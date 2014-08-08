
#ifndef VOTE_REQUEST_HPP_
#define VOTE_REQUEST_HPP_

#include "raft/i_message.hpp"

namespace raft {

class VoteRequest :public IMessage{
public:
	static const char* const TYPE_NAME ;
public:
	VoteRequest();
	VoteRequest(uint64_t Term,uint64_t LastLogIndex,uint64_t LastLogTerm,const std::string& name);
	~VoteRequest();
	uint64_t Term;
	uint64_t LastLogIndex;
	uint64_t LastLogTerm;
	std::string CandidateName;
	virtual const char* TypeName(){
		return TYPE_NAME;
	}
	virtual bool Encode(abb::Buffer& buf);
	virtual bool Decode(abb::Buffer& buf);
};

class VoteResponce :public IMessage{
public:
	static const char* const TYPE_NAME;
public:
	VoteResponce();
	VoteResponce(uint64_t Term,bool VoteGranted);
	~VoteResponce();
	uint64_t Term;
	bool VoteGranted;
	uint64_t snd_term;
	virtual const char* TypeName(){
		return TYPE_NAME;
	}
	virtual bool Encode(abb::Buffer& buf);
	virtual bool Decode(abb::Buffer& buf);
};

} /* namespace raft */

#endif /* VOTE_REQUEST_HPP_ */
