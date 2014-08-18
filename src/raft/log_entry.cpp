
#include "log_entry.hpp"
#include <cassert>
namespace raft {
const char* const LogEntry::TYPE_NAME = "LogEntry";
LogEntry::LogEntry()
:log_(NULL),
index_(0),
term_(0),
position(0),
cmd_(NULL),
ev_(NULL){

}
LogEntry::LogEntry(LogManager* log,uint64_t index,uint64_t term,Commond*cmd,EventBase* ev)
:log_(log),
index_(index),
term_(term),
position(0),
cmd_(cmd),
ev_(ev)
{
	ev_->Ref();
	cmd_->Ref();
	cmd_->Encode(cmd_buf);
}

LogEntry::~LogEntry() {
	if(ev_)ev_->UnRef();
	if(cmd_)cmd_->UnRef();
}
bool LogEntry::Encode(abb::Buffer& buf){
	buf.NET_WriteUint64(index_);
	buf.NET_WriteUint64(term_);
	buf << cmd_->CommondName();
	buf.NET_WriteUint32(cmd_buf.ReadSize());
	buf.Write(cmd_buf.ReadPtr(),cmd_buf.ReadSize());
	return true;
}
bool LogEntry::Decode(abb::Buffer& buf){
	index_ = buf.HOST_ReadUint64();
	term_ = buf.HOST_ReadUint64();
	std::string name;
	buf >> name;
	uint32_t size = buf.HOST_ReadUint32();
	cmd_buf.Clear();
	cmd_buf.Write(buf.ReadPtr(),size);
	buf.GaveRead(size);
	cmd_ = NewCommond(name);
	bool ok = cmd_->Decode(cmd_buf);
	cmd_buf.BackRead(size-cmd_buf.ReadSize());
	return ok;
}

} /* namespace adcloud */
