#include "raft/commonds.hpp"
#include "server.hpp"
#include "raft/messages.hpp"
#include "abb/base/log.hpp"
namespace raft {
const char* const JoinCommond::CMD_NAME = "JoinCommond" ;

void JoinCommond::Apply(IServer* s,std::string *save_error,IMessage**ret){
	Server* svr = static_cast<Server*>(s);
	svr->AddPeer(this->name_,this->addr_);
	if(cmd_){
		cmd_->Apply(s,NULL,NULL);
	}
	if(ret){
		*ret = new BoolMessage(true);
	}
}
bool JoinCommond::Encode(abb::Buffer& buf){
	buf << name_ << addr_;
	buf << cmd_->CommondName();
	cmd_->Encode(buf);
	return true;
};
bool JoinCommond::Decode(abb::Buffer& buf){
	buf >> name_ >> addr_;
	std::string cmdname;
	buf >> cmdname;
	cmd_ = NewCommond(cmdname);
	return cmd_->Decode(buf);
};


}
