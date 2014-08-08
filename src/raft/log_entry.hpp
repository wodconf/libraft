
#ifndef AD_CLOUD_RAFT_RAFT_LOG_ENTRY_HPP_
#define AD_CLOUD_RAFT_RAFT_LOG_ENTRY_HPP_

#include <stdint.h>
#include "event.hpp"
#include "raft/commond.hpp"
#include "raft/i_message.hpp"
namespace raft {
class RaftLog;
class LogEntry :public IMessage{
public:
	static const char* const TYPE_NAME = "LogEntry";
public:
	LogEntry();
	LogEntry(RaftLog* log,uint64_t index,uint64_t term,Commond*cmd,Event* ev);
	~LogEntry();
	RaftLog* log_;
	uint64_t index_;
	uint64_t term_;
	Commond* cmd_;
	Event* ev_;
	int position;
	virtual const char* TypeName(){
		return TYPE_NAME;
	}
	virtual bool Encode(abb::Buffer& buf){return true;};
	virtual bool Decode(abb::Buffer& buf){return true;};
};




}

#endif /* RAFT_LOG_ENTRY_HPP_ */
