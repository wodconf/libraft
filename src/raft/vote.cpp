
#include "vote.hpp"

namespace raft {
VoteRequest::VoteRequest()
:Term(0),LastLogIndex(0),LastLogTerm(0)
{
}
VoteRequest::VoteRequest(uint64_t Term,
		uint64_t LastLogIndex,
		uint64_t LastLogTerm,
		const std::string& name)
:Term(Term),LastLogIndex(LastLogIndex),LastLogTerm(LastLogTerm),CandidateName(name)
{
}

VoteRequest::~VoteRequest() {

}
bool VoteRequest::Encode(abb::Buffer& buf){
	buf.NET_WriteUint64(Term);
	buf.NET_WriteUint64(LastLogIndex);
	buf.NET_WriteUint64(LastLogTerm);
	buf << CandidateName;
	return true;
}
bool VoteRequest::Decode(abb::Buffer& buf){
	Term = buf.HOST_ReadUint64();
	LastLogIndex = buf.HOST_ReadUint64();
	LastLogTerm = buf.HOST_ReadUint64();
	buf >> CandidateName;
	return true;
}

VoteResponce::VoteResponce(uint64_t Term,bool VoteGranted):Term(Term),VoteGranted(VoteGranted),snd_term(0) {

}
VoteResponce::VoteResponce():Term(0),VoteGranted(false),snd_term(0)  {

}

VoteResponce::~VoteResponce() {

}
bool VoteResponce::Encode(abb::Buffer& buf){
	buf.NET_WriteUint64(Term);
	buf << VoteGranted;
	return true;
}
bool VoteResponce::Decode(abb::Buffer& buf){
	Term = buf.HOST_ReadUint64();
	buf >> VoteGranted;
	return true;
}


const char* const VoteRequest::TYPE_NAME = "VoteRequest";
const char* const  VoteResponce::TYPE_NAME = "VoteResponce";

} /* namespace raft */
