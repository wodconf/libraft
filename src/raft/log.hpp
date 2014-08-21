
#ifndef ADCLOUD_RAFT_LOG_HPP_
#define ADCLOUD_RAFT_LOG_HPP_
#include <abb/base/thread.hpp>
#include "log_entry.hpp"
#include <stdint.h>
#include <stdio.h>
#include <sys/uio.h>
#include <vector>
namespace raft {
class LogEntry;
class IServer;
class LogManager {
public:
	typedef std::vector<LogEntry*> LogEntryArray;
	LogManager(IServer* svr);
	~LogManager();
	bool Open(const std::string& path);
	void Close();
	LogEntry* GetLogEntry(uint64_t index);
	uint64_t GetCommitIndex(){
		abb::Mutex::Locker l(mtx_);
		return commit_index_;
	}
	LogEntry* CreateEntry(uint64_t term,Commond* cmd,EventBase* ev);
	bool AppendEntry(LogEntry* entry,std::string* save_err);
	bool AppendEntries(LogEntryArray entries,std::string* save_err);
	bool GetEntriesAfter(int max,uint64_t index,LogEntryArray* arr,uint64_t* term);

	void GetCommitInfo(uint64_t* index,uint64_t* term);
	void GetLastInfo(uint64_t* index,uint64_t* term);

	void UpdateCommitIndex(uint64_t index);
	bool CommitToIndex(uint64_t index);
	bool Truncate(uint64_t index,uint64_t term);
	void SetStartInfo(uint64_t index,uint64_t term){
		this->start_index_ = index;
		this->start_term_ = term;
	}
	void GetStartInfo(uint64_t* index,uint64_t* term){
		if(index)*index = this->start_index_;
		if(term)*term = this->start_term_;
	}
	uint64_t GetCurrentIndex();
	uint64_t GetNextIndex();
	bool IsEmpty();
	bool Compact(uint64_t index,uint64_t term);
private:
	static bool WriteEntry(int fd,LogEntry* entry);
	uint64_t InternalGetCurrentIndex(){
		if(log_entry_arr_.size() == 0){
			return start_index_;
		}
		return log_entry_arr_.back()->index_;
	}
	static int StaticReader(void*arg,const struct iovec *iov, int iovcnt){
		LogManager* log = (LogManager*)arg;
		return log->Reader(iov,iovcnt);
	}
	int Reader(const struct iovec *iov, int iovcnt);
	
	abb::Mutex mtx_;
	IServer* svr_;
	uint64_t commit_index_;//
	uint64_t start_index_;
	uint64_t start_term_;
	int fd_;
	int err_;
	std::string path_;
	
	LogEntryArray log_entry_arr_;
};

} /* namespace translate */

#endif /* RAFT_LOG_HPP_ */
