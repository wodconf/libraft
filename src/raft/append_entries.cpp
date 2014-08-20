
#include "append_entries.hpp"
#include "log_entry.hpp"
#include <cassert>
namespace raft {
const char* const AppendEntriesRequest::TYPE_NAME = "AppendEntriesRequest";
const char* const AppendEntriesResponce::TYPE_NAME = "AppendEntriesResponce";
AppendEntriesRequest::AppendEntriesRequest()
:
 term(0),
 PrevLogIndex(0),
 PrevLogTerm(0),
 CommitIndex(0)
{

}
AppendEntriesRequest::AppendEntriesRequest(uint64_t term,
		uint64_t PrevLogIndex,
		uint64_t PrevLogTerm,
		uint64_t CommitIndex,
		const std::string& LeaderName)
:
 term(term),
 PrevLogIndex(PrevLogIndex),
 PrevLogTerm(PrevLogTerm),
 CommitIndex(CommitIndex),
 LeaderName(LeaderName)
{

}
AppendEntriesRequest::AppendEntriesRequest(uint64_t term,
		uint64_t PrevLogIndex,
		uint64_t PrevLogTerm,
		uint64_t CommitIndex,
		const std::string& LeaderName,
		const LogEntryArray& arr)
:
 term(term),
 PrevLogIndex(PrevLogIndex),
 PrevLogTerm(PrevLogTerm),
 CommitIndex(CommitIndex),
 LeaderName(LeaderName),
 Entries(arr)
{
}

AppendEntriesRequest::~AppendEntriesRequest() {
	for(unsigned i=0;i<Entries.size();i++){
		Entries[i]->UnRef();
	}
}
bool AppendEntriesRequest::Encode(abb::Buffer &buf){
	buf.NET_WriteUint64(term);
	buf.NET_WriteUint64(PrevLogIndex);
	buf.NET_WriteUint64(PrevLogTerm);
	buf.NET_WriteUint64(CommitIndex);
	buf << LeaderName;
	buf.NET_WriteUint32(uint32_t(Entries.size()));
	for(unsigned i=0;i<Entries.size();i++){
		if(!Entries[i]->Encode(buf)) return false;
	}
	return true;
}
bool AppendEntriesRequest::Decode(abb::Buffer &buf){
	term = buf.HOST_ReadUint64();
	PrevLogIndex = buf.HOST_ReadUint64();
	PrevLogTerm = buf.HOST_ReadUint64();
	CommitIndex = buf.HOST_ReadUint64();
	buf >> LeaderName;
	uint32_t size = buf.HOST_ReadUint32();
	for(unsigned i=0;i<size;i++){
		LogEntry* entry = new LogEntry();
		if(!entry->Decode(buf)){
			entry->UnRef();
			return false;
		}
		Entries.push_back(entry);
	}
	return true;
}


AppendEntriesResponce::AppendEntriesResponce()
:Term(0),
 Index(0),
 Success(false),
 CommitIndex(0),
 append(false){

}
AppendEntriesResponce::AppendEntriesResponce(uint64_t Term,uint64_t Index,bool Success,uint64_t CommitIndex)
:Term(Term),
 Index(Index),
 Success(Success),
 CommitIndex(CommitIndex),
 append(false){

}

AppendEntriesResponce::~AppendEntriesResponce() {
}
bool AppendEntriesResponce::Encode(abb::Buffer &buf){
	buf.NET_WriteUint64(Term);
	buf.NET_WriteUint64(Index);
	buf.NET_WriteUint64(CommitIndex);
	buf << Success;
	return true;
}
bool AppendEntriesResponce::Decode(abb::Buffer &buf){
	Term = buf.HOST_ReadUint64();
	Index = buf.HOST_ReadUint64();
	CommitIndex = buf.HOST_ReadUint64();
	buf >> Success;
	return true;
}
} /* namespace adcloud */
