
#include "log.hpp"
#include <abb/base/log.hpp>
#include <errno.h>
#include <string.h>
#include <abb/base/buffer.hpp>
#include <fcntl.h>
#include <unistd.h>
namespace raft {

LogManager::LogManager(IServer* svr)
:svr_(svr),
commit_index_(0),
start_index_(0),start_term_(0),fd_(-1) ,err_(-1){

}

LogManager::~LogManager() {

}
int LogManager::Reader(const struct iovec *iov, int iovcnt){
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
bool LogManager::Open(const std::string& path){
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
	while( buf.WriteFromeReader(StaticReader,this) > 0){
		;
	}
	int position = 0;
	int size = buf.ReadSize();
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
		position += size - buf.ReadSize();
		entry->UnRef();
		size = buf.ReadSize();
	}
	if(this->log_entry_arr_.size() > 0){
		LOG(DEBUG) << "open.log.append log index " << this->log_entry_arr_.front()->index_ << ":" <<this->log_entry_arr_.back()->index_ << "  " << this->commit_index_;
	}
	
	return true;
}
void LogManager::Close(){
	if(fd_>0)
		close(fd_);
}
LogEntry* LogManager::GetLogEntry(uint64_t index){
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
bool LogManager::Compact(uint64_t index,uint64_t term){
	LOG(DEBUG) << "Compact.index" << index;
	LogEntryArray  arr;
	abb::Mutex::Locker l(mtx_);
	if(index == 0){
		return true;
	}
	if(index < this->InternalGetCurrentIndex()){
		for(unsigned i=index-start_index_;i<this->log_entry_arr_.size();i++){
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
	for(unsigned i=0;i<arr.size();i++){
		if(!WriteEntry(fd,arr[i])){
			close(fd);
			remove(newpath.c_str());
			for(unsigned j=0;j<arr.size();j++){
				arr[j]->UnRef();
			}
			return false;
		}
	}
	fsync(fd);
	close(fd_);
	this->fd_ = fd;
	rename(newpath.c_str(),path_.c_str());
	for(unsigned i=0;i<this->log_entry_arr_.size();i++){
		log_entry_arr_[i]->UnRef();
	}
	this->log_entry_arr_ = arr;
	this->start_index_ = index;
	this->start_term_ = term;
	LOG(DEBUG) << "Compact.success.index = " << index <<".start_index = "<< this->start_index_ << ".entry_arr_size = " << log_entry_arr_.size();
	return true;

}
bool  LogManager::WriteEntry(int fd,LogEntry* entry){
	entry->position = lseek(fd,0,SEEK_CUR);
	abb::Buffer buf;
	entry->Encode(buf);
	while(true){
		int ret = write(fd,(void*)buf.ReadPtr(),buf.ReadSize());
		if(ret < 0 ){
			if(errno == EINTR){
				continue;
			}else{
				return false;
			}
		}else{
			break;
		}
	}
	return true;
}
LogEntry* LogManager::CreateEntry(uint64_t term,Commond* cmd,EventBase* ev){
	LogEntry* entry = new LogEntry(this,GetNextIndex(),term,cmd,ev);
	return entry;
}
bool LogManager::AppendEntry(LogEntry* entry,std::string* save_err){
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
	WriteEntry(fd_,entry);
	this->log_entry_arr_.push_back(entry);
	return true;
}
bool LogManager::AppendEntries(LogEntryArray entries,std::string* save_err){
	abb::Mutex::Locker l(mtx_);
	for(unsigned i=0;i<entries.size();i++){
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
		WriteEntry(fd_,entry);
		this->log_entry_arr_.push_back(entry);
	}
	if(entries.size() > 0)
		fsync(fd_);
	return true;
}
bool LogManager::GetEntriesAfter(int max,uint64_t index,LogEntryArray* arr,uint64_t* term){
	abb::Mutex::Locker l(mtx_);
	if(index < this->start_index_){
		LOG(DEBUG) << "log.entriesAfter.before: " << index<<  " "<< start_index_;
		*term = 0;
		return true;
	}
	if (index > (log_entry_arr_.size() + start_index_) ) {
		LOG(DEBUG) << "Index is beyond end of log: " << index<<  " "<< log_entry_arr_.size();
		return false;
	}
	if (index == this->start_index_) {
		for(unsigned i=0;i<log_entry_arr_.size() && max > 0;i++,max--){
			this->log_entry_arr_[i]->Ref();
			arr->push_back(log_entry_arr_[i]);
		}
		*term = this->start_term_;
		return true;
	}
	int st = index - this->start_index_;
	*term = this->log_entry_arr_[st-1]->term_;
	for(;st < (int)this->log_entry_arr_.size() && max > 0;st++,max--){
		log_entry_arr_[st]->Ref();
		arr->push_back(log_entry_arr_[st]);
	}
	return true;
}

void LogManager::GetCommitInfo(uint64_t* index,uint64_t* term){
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
void LogManager::GetLastInfo(uint64_t* index,uint64_t* term){
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

void LogManager::UpdateCommitIndex(uint64_t index){
	abb::Mutex::Locker l(mtx_);
	if(index > this->commit_index_){
		this->commit_index_ = index;
	}
	LOG(DEBUG) <<"update.commit.index "<< index <<":" << this->commit_index_;
}
bool LogManager::CommitToIndex(uint64_t index){
	LogEntryArray arr;
	{
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
		for(unsigned i= this->commit_index_+1;i<=index;i++){
			int e_index = i - 1 - this->start_index_;
			LogEntry* entry = log_entry_arr_[e_index];
			this->commit_index_ = entry->index_;
			entry->Ref();
			arr.push_back(entry);
		}
	}
	for(unsigned i = 0;i< arr.size();i++){
		LogEntry* entry = arr[i];
		if(entry->ev_){
			IMessage* msg = NULL;
			std::string error ;
			entry->cmd_->Apply(this->svr_,&error,&msg);
			entry->ev_->Notify(msg,error);
			entry->ev_->UnRef();
			entry->ev_ = NULL;
		}else{
			entry->cmd_->Apply(this->svr_,NULL,NULL);
		}
		entry->UnRef();
	}
	return true;
}
bool LogManager::Truncate(uint64_t index,uint64_t term){
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
		ftruncate(fd_,0);
		for(unsigned st=0;st < this->log_entry_arr_.size() ;st++){
			LogEntry* entry = log_entry_arr_[st];
			if(entry->ev_){
				if(entry->ev_){
					entry->ev_->Notify(NULL,"command failed to be committed due to node failure");
					entry->ev_->UnRef();
					entry->ev_ = NULL;
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
				ftruncate(fd_,entry->position);
				lseek(fd_,entry->position, SEEK_SET);
				for(int i=start;i < (int)this->log_entry_arr_.size();i++){
					LogEntry* inentry = this->log_entry_arr_[i];
					if(inentry){
						if(inentry->ev_){
							inentry->ev_->Notify(NULL,"command failed to be committed due to node failure");
							inentry->ev_->UnRef();
							inentry->ev_ = NULL;
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
uint64_t LogManager::GetCurrentIndex(){
	abb::Mutex::Locker l(mtx_);
	return InternalGetCurrentIndex();
}
uint64_t LogManager::GetNextIndex(){
	return this->GetCurrentIndex()+1;
}
bool LogManager::IsEmpty(){
	abb::Mutex::Locker l(mtx_);
	return (log_entry_arr_.size() == 0) &&( start_index_ == 0 );
}
} 
