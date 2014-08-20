

#include "raft/i_server.hpp"
#include "raft/i_state_machine.hpp"
#include <abb/http/http_server.hpp>
#include <abb/base/log.hpp>
class StateMachine:public raft::IStateMachine ,public abb::http::Server::Listener ,public raft::IFactory{
public:
	virtual ~StateMachine(){}
	virtual bool Save(abb::Buffer& buf){

	}
	virtual bool Recovery(abb::Buffer& buf){

	}
	virtual void HandleRequest(abb::http::Request* req,abb::http::ResponceWriter* rspw){

	}
	virtual raft::Commond* GetJoinCmd(){
		return NULL;
	}
};

StateMachine st;
int main(int argc,const char* argv[]){
	abb::g_min_log_level = abb::LOGLEVEL_DEBUG;
	std::string addr1 = "localhost:5303";
	std::string addr2 = "localhost:5403";
	raft::IServer* svr;
	std::vector<std::string> v;
	raft::Config cfg;
	if(argc == 2){
		svr  = raft::NewServer("name2","./name2",addr2,NULL,&st,cfg,&st);
		v.push_back(addr1);
	}else{
		svr  = raft::NewServer("name1","./name1",addr1,NULL,&st,cfg,&st);
	}
	
	if( !svr->Init() ){
		return -1;
	}
	
	if( !svr->Start(v) ){
		return -2;
	}
	abb::http::Server http_svr;
	http_svr.Init(1,true);
	http_svr.SetListener(&st);
	abb::net::IPAddr addr;
	addr.SetV4(NULL,8083);
	http_svr.Bind(addr,NULL);
	http_svr.Start();
}

