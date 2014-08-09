
#include "log_entry.hpp"
#include <cassert>
namespace raft {
const char* const LogEntry::TYPE_NAME = "LogEntry";
LogEntry::LogEntry():log_(NULL),index_(0),term_(0),position(0),cmd_(NULL),ev_(NULL){

}
LogEntry::LogEntry(RaftLog* log,uint64_t index,uint64_t term,Commond*cmd,Event* ev)
:log_(log),index_(index),term_(term),cmd_(cmd),ev_(ev),position(0){
	ev_->Ref();
}

LogEntry::~LogEntry() {
	if(ev_)ev_->notify_.Notify();
	if(ev_)ev_->UnRef();
}
bool LogEntry::Encode(abb::Buffer& buf){
	buf.NET_WriteUint64(index_);
	buf.NET_WriteUint64(term_);
	buf << cmd_->CommondName();
	return cmd_->Encode(buf);
}
bool LogEntry::Decode(abb::Buffer& buf){
	index_ = buf.HOST_ReadUint64();
	term_ = buf.HOST_ReadUint64();
	std::string name;
	buf >> name;
	cmd_ = NewCommond(name);
	return cmd_->Decode(buf);

}

} /* namespace adcloud */
