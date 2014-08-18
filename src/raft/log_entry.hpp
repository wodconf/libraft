
#ifndef AD_CLOUD_RAFT_RAFT_LOG_ENTRY_HPP_
#define AD_CLOUD_RAFT_RAFT_LOG_ENTRY_HPP_

#include <stdint.h>
#include "event.hpp"
#include "raft/commond.hpp"
#include "raft/i_message.hpp"
namespace raft {
class LogManager;
class LogEntry :public IMessage{
public:
	static const char* const TYPE_NAME;
public:
	LogEntry();
	LogEntry(LogManager* log,uint64_t index,uint64_t term,Commond*cmd,EventBase* ev);
	~LogEntry();
	virtual const char* TypeName(){
		return TYPE_NAME;
	}
	virtual bool Encode(abb::Buffer& buf);
	virtual bool Decode(abb::Buffer& buf);
public:
	LogManager* log_;
	uint64_t index_;
	uint64_t term_;
	int position;
	Commond* cmd_;
	EventBase* ev_;
	abb::Buffer cmd_buf;
};




}

#endif /* RAFT_LOG_ENTRY_HPP_ */
