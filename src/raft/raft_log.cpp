
#include "raft_log.hpp"
#include <abb/base/log.hpp>
#include <errno.h>
#include <string.h>
#include <abb/base/buffer.hpp>
#include <fcntl.h>
#include <unistd.h>
namespace raft {

RaftLog::RaftLog(RaftServer* svr):svr_(svr),commit_index_(0),start_index_(0),start_term_(0),fd_(-1) ,err_(-1){

}

RaftLog::~RaftLog() {

}
int RaftLog::Reader(const struct iovec *iov, int iovcnt){
	int ret;
	int nrd = 0;
	while(true){
		ret = readv(fd_, iov, iovcnt);
		if(ret < 0){
			int err = errno;
			if(err == EINTR){
				continue;
			}else{
				this->err_ = err;
				break;
			}
		}
		nrd = ret;
		break;
	}
	return nrd;
}
bool RaftLog::Open(const std::string& path){
	path_ = path;
	abb::Mutex::Locker l(mtx_);
	fd_ = open(path.c_str(),O_RDWR,0600);
	if(fd_ < 0){
		LOG(WARN)<< "raft.open.file.failed:"<<path<< " err:" <<errno<<" errstr:" << strerror(errno);
		fd_ = open(path.c_str(),O_RDWR|O_CREAT,0600);
		if(fd_ < 0){
			LOG(WARN)<< "raft.create.file.failed:"<<path<< " err:" <<errno<<" errstr:" << strerror(errno);
			return false;
		}
		LOG(DEBUG)<< "log.open.create " << path;
		return true;
	}
	fcntl(fd_, F_SETFD, FD_CLOEXEC);
	LOG(DEBUG)<< "log.open.exist " << path;
	lseek(fd_,0,SEEK_SET);
	abb::Buffer buf;
	buf.WriteFromeReader(StaticReader,this);
	int position = 0;
	int size = buf.Size();
	while(size > 0){
		LogEntry* entry = new LogEntry();
		if( ! entry->Decode(buf) ) {
			entry->UnRef();
			return false;
		}

		entry->position = position;
		if(entry->index_ > this->start_index_){
			entry->Ref();
			this->log_entry_arr_.push_back(entry);
			if(entry->index_ <= this->commit_index_){
				entry->cmd_->Apply(this->svr_,NULL,NULL);
			}
		}
		position += size - buf.Size();
		entry->UnRef();
		size = buf.Size();
	}
	if(this->log_entry_arr_.size() > 0){
		LOG(DEBUG) << "open.log.append log index " << this->log_entry_arr_.front()->index_ << ":" <<this->log_entry_arr_.back()->index_ << "  " << this->commit_index_;
	}
	
	return true;
}
void RaftLog::Close(){
	if(fd_>0)
		close(fd_);
}
LogEntry* RaftLog::GetLogEntry(uint64_t index){
	abb::Mutex::Locker l(mtx_);
	if(index <= this->start_index_){
		LOG(TRACE) << "log.GetLogEntry.before: " << index<<  " "<< start_index_;
		return NULL;
	}
	if (index > (log_entry_arr_.size() + start_index_) ) {
		LOG(TRACE) << "log.GetLogEntry Index is beyond end of log: " << index<<  " "<< log_entry_arr_.size();
		return NULL;
	}
	int st = index - this->start_index_;
	return	this->log_entry_arr_[st-1];
}
bool RaftLog::Compact(uint64_t index,uint64_t term){
	LOG(DEBUG) << "Compact.index" << index;
	LogEntryArray  arr;
	abb::Mutex::Locker l(mtx_);
	if(index == 0){
		return true;
	}
	if(index < this->InternalGetCurrentIndex()){
		for(int i=index-start_index_;i<this->log_entry_arr_.size();i++){
			log_entry_arr_[i]->Ref();
			arr.push_back(log_entry_arr_[i]);
		}
	}
	std::string newpath = this->path_ + ".new";
	int fd =  open(newpath.c_str(),O_WRONLY|O_APPEND|O_CREAT,0600);
	if(fd < 0){
		LOG(WARN) << "Open Fial fail: path:" << newpath << "err:" << strerror(errno);
		return false;
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	for(int i=0;i<arr.size();i++){
		if(!WriteEntry(fd,arr[i],false)){
			close(fd);
			remove(newpath.c_str());
			for(int i=0;i<arr.size();i++){
				arr[i]->UnRef();
			}
			return false;
		}
	}
	fsync(fd);
	close(fd_);
	this->fd_ = fd;
	rename(newpath.c_str(),path_.c_str());
	for(int i=0;i<this->log_entry_arr_.size();i++){
		log_entry_arr_[i]->UnRef();
	}
	this->log_entry_arr_ = arr;
	this->start_index_ = index;
	this->start_term_ = term;
	LOG(DEBUG) << "Compact.success.index = " << index <<".start_index = "<< this->start_index_ << ".entry_arr_size = " << log_entry_arr_.size();
	return true;

}
bool  RaftLog::WriteEntry(int fd,LogEntry* entry,bool bsync){
	entry->position = lseek(fd,0,SEEK_CUR);
	abb::Buffer buf;
	entry->Encode(buf);
	while(true){
		int ret = write(fd,(void*)buf.Data(),buf.Size());
		if(ret < 0 ){
			if(errno == EINTR){
				continue;
			}else{
				delete buf;
				return false;
			}
		}else{
			break;
		}
	}
	if(bsync)fsync(fd);
	delete buf;
	return true;
}
LogEntry* RaftLog::CreateEntry(uint64_t term,Commond* cmd,Event* ev){
	LogEntry* entry = new LogEntry(this,GetNextIndex(),term,cmd,ev);
	return entry;
}
bool RaftLog::AppendEntry(LogEntry* entry,std::string* save_err){
	abb::Mutex::Locker l(mtx_);
	if(this->log_entry_arr_.size() > 0){
		LogEntry* last_entry = this->log_entry_arr_.back();
		if(last_entry->term_ > entry->term_){
			LOG(DEBUG) << "Cannot Append entry with earlier term last:" << last_entry->term_ << "insert:" << entry->term_;
			if(save_err) *save_err = "Cannot Append entry with earlier term";
			return false;
		}else if(last_entry->term_ == entry->term_ && entry->index_ < last_entry->index_){
			LOG(DEBUG) << "Cannot Append entry with earlier index in same term last:" << last_entry->index_ << "insert:" << entry->index_;
			if(save_err) *save_err = "Cannot Append entry with earlier index";
			return false;
		}
	}
	WriteEntry(fd_,entry,true);
	this->log_entry_arr_.push_back(entry);
	return true;
}
bool RaftLog::AppendEntries(LogEntryArray entries,std::string* save_err){
	abb::Mutex::Locker l(mtx_);
	for(int i=0;i<entries.size();i++){
		LogEntry* entry = entries[i];
		if(this->log_entry_arr_.size() > 0){
			LogEntry* last_entry = this->log_entry_arr_.back();
			if(last_entry->term_ > entry->term_){
				LOG(DEBUG) << "Cannot Append entry with earlier term last:" << last_entry->term_ << "insert:" << entry->term_;
				if(save_err) *save_err = "Cannot Append entry with earlier term";
				return false;
			}else if(last_entry->term_ == entry->term_ && entry->index_ < last_entry->index_){
				LOG(DEBUG) << "Cannot Append entry with earlier index in same term last:" << last_entry->index_ << "insert:" << entry->index_;
				if(save_err) *save_err = "Cannot Append entry with earlier index";
				return false;
			}
		}
		entry->Ref();
		WriteEntry(fd_,entry,false);
		this->log_entry_arr_.push_back(entry);
	}
	if(entries.size() > 0)
		fsync(fd_);
	return true;
}
bool RaftLog::GetEntriesAfter(int max,uint64_t index,LogEntryArray* arr,uint64_t* term){
	abb::Mutex::Locker l(mtx_);
	if(index < this->start_index_){
		LOG(TRACE) << "log.entriesAfter.before: " << index<<  " "<< start_index_;
		*term = 0;
		return true;
	}
	if (index > (log_entry_arr_.size() + start_index_) ) {
		LOG(TRACE) << "Index is beyond end of log: " << index<<  " "<< log_entry_arr_.size();
		return false;
	}
	if (index == this->start_index_) {
		*arr = this->log_entry_arr_;
		*term = this->start_term_;
		return true;
	}
	int st = index - this->start_index_;
	*term = this->log_entry_arr_[st-1]->term_;
	for(;st < this->log_entry_arr_.size() && max > 0;st++,max--){
		arr->push_back(log_entry_arr_[st]);
	}
	return true;
}

void RaftLog::GetCommitInfo(uint64_t* index,uint64_t* term){
	abb::Mutex::Locker l(mtx_);
	if(this->commit_index_ == 0){
		if(index)*index = 0;
		if(term)*term = 0;
		return;
	}
	if(this->commit_index_ == this->start_index_){
		if(index)*index =start_index_;
		if(term)*term =start_term_ ;
		return;
	}

	LOG(DEBUG) << "ci:" << commit_index_ << "star:" << this->start_index_ << "size:" << this->log_entry_arr_.size() << "first_index:" << this->log_entry_arr_[0]->index_;
	int st = commit_index_ - this->start_index_ -1;
	LogEntry* entry = this->log_entry_arr_[st];
	if(index)*index =entry->index_;
	if(term)*term =entry->term_ ;
	LOG(DEBUG) << "commitInfo.get.["  << commit_index_ << "/" << this->start_index_ << "]";
}
void RaftLog::GetLastInfo(uint64_t* index,uint64_t* term){
	abb::Mutex::Locker l(mtx_);
	if(this->log_entry_arr_.size() == 0){
		if(index)*index =start_index_;
		if(term)*term =start_term_ ;
		return;
	}
	LogEntry* entry = this->log_entry_arr_.back();
	if(index)*index =entry->index_;
	if(term)*term =entry->term_ ;
}

void RaftLog::UpdateCommitIndex(uint64_t index){
	abb::Mutex::Locker l(mtx_);
	if(index > this->commit_index_){
		this->commit_index_ = index;
	}
	LOG(DEBUG) <<"update.commit.index "<< index <<":" << this->commit_index_;
}
bool RaftLog::CommitToIndex(uint64_t index){
	LOG(TRACE) << "CommitToIndex" << index;
	abb::Mutex::Locker l(mtx_);
	if(index > (log_entry_arr_.size() + start_index_)){
		LOG(DEBUG) << "raft.Log: Commit index" << index << "set back to " << ( log_entry_arr_.size() + start_index_);
		index = log_entry_arr_.size() + start_index_;
	}
	if (index < commit_index_) {
		LOG(DEBUG) << "raft.Log: Commit index earily :" << index << "commit_index_:" << commit_index_;
		return true;
	}
	for(int i= this->commit_index_+1;i<=index;i++){
		int e_index = i - 1 - this->start_index_;
		LogEntry* entry = log_entry_arr_[e_index];
		this->commit_index_ = entry->index_;
		if(entry->ev_){
			entry->cmd_->Apply(this->svr_,&entry->ev_->err,&entry->ev_->rsp);
			entry->ev_->Notify();
		}else{
			entry->cmd_->Apply(this->svr_,NULL,false);
		}
	}
	return true;
}
bool RaftLog::Truncate(uint64_t index,uint64_t term){
	abb::Mutex::Locker l(mtx_);
	LOG(TRACE) << "log.truncate: " <<  index;
	if(index < this->commit_index_){
		LOG(DEBUG) << "log.truncate.before";
		return false;
	}
	if(index > (log_entry_arr_.size() + start_index_)){
		LOG(DEBUG) << "log.truncate.after" << index << ":"<< log_entry_arr_.size() << "+" << start_index_ << "commit_index:" << commit_index_;
		return false;
	}
	if(index == this->start_index_){
		lseek(fd_,0, SEEK_SET);
		int ret = ftruncate(fd_,0);
		for(int st=0;st < this->log_entry_arr_.size() ;st++){
			LogEntry* entry = log_entry_arr_[st];
			if(entry->ev_){
				if(entry->ev_){
					entry->ev_->err = "command failed to be committed due to node failure";
					entry->ev_->notify_.Notify();
				}
				entry->UnRef();
			}
		}
		this->log_entry_arr_.clear();
	}else{
		if(index < (this->start_index_+this->log_entry_arr_.size()) ){
			int st = index - this->start_index_ -1;
			LogEntry* entry = this->log_entry_arr_[st];
			if (this->log_entry_arr_.size()> 0 && entry->term_ != term ){
				LOG(DEBUG) << "log.truncate.termMismatch";
				return false;
			}else{
				int start = index - this->start_index_;
				LogEntry* entry = this->log_entry_arr_[start];
				int ret = ftruncate(fd_,entry->position);
				lseek(fd_,entry->position, SEEK_SET);
				for(int i=start;i < this->log_entry_arr_.size();i++){
					LogEntry* inentry = this->log_entry_arr_[i];
					if(inentry){
						if(inentry->ev_){
							inentry->ev_->err = "command failed to be committed due to node failure";
							inentry->ev_->notify_.Notify();
						}
						inentry->UnRef();
					}
				}
				this->log_entry_arr_.erase(this->log_entry_arr_.begin()+start,this->log_entry_arr_.end());
				LOG(DEBUG) << "log.truncate.success" << this->start_index_  << "/" << this->log_entry_arr_.size();
			}
		}
	}
	return true;
}
uint64_t RaftLog::GetCurrentIndex(){
	abb::Mutex::Locker l(mtx_);
	return InternalGetCurrentIndex();
}
uint64_t RaftLog::GetNextIndex(){
	return this->GetCurrentIndex()+1;
}
bool RaftLog::IsEmpty(){
	abb::Mutex::Locker l(mtx_);
	return (log_entry_arr_.size() == 0) &&( start_index_ == 0 );
}
} /* namespace translate */
