
#ifndef APPEND_ENTRIES_REQUEST_HPP_
#define APPEND_ENTRIES_REQUEST_HPP_

#include <vector>
#include "raft/i_message.hpp"

namespace raft {
class LogEntry;
class AppendEntriesRequest:public IMessage{
public:
	static const char* const TYPE_NAME ;
public:
	typedef std::vector<LogEntry*> LogEntryArray;
	AppendEntriesRequest();
	AppendEntriesRequest(uint64_t term,
			uint64_t PrevLogIndex,
			uint64_t PrevLogTerm,
			uint64_t CommitIndex,
			const std::string& LeaderName);
	AppendEntriesRequest(uint64_t term,
			uint64_t PrevLogIndex,
			uint64_t PrevLogTerm,
			uint64_t CommitIndex,
			const std::string& LeaderName,
			const LogEntryArray& arr);
	~AppendEntriesRequest();
	virtual const char* TypeName(){
		return TYPE_NAME;
	}
	virtual bool Encode(abb::Buffer& buf);
	virtual bool Decode(abb::Buffer& buf);
	uint64_t term;
	uint64_t PrevLogIndex;
	uint64_t PrevLogTerm;
	uint64_t CommitIndex;
	std::string LeaderName;
	LogEntryArray Entries;
};

class AppendEntriesResponce:public IMessage {
public:
	static const char* const TYPE_NAME;
public:
	AppendEntriesResponce();
	AppendEntriesResponce(uint64_t Term,uint64_t Index,bool Success,uint64_t CommitIndex);
	~AppendEntriesResponce();
public:
	virtual const char* TypeName(){
		return TYPE_NAME;
	}
	virtual bool Encode(abb::Buffer& buf);
	virtual bool Decode(abb::Buffer& buf);
	uint64_t Term;
	uint64_t Index;
	bool Success;
	uint64_t CommitIndex;
	//
	std::string peer;
	bool append;
};
} /* namespace raft */

#endif /* APPEND_ENTRIES_REQUEST_HPP_ */
