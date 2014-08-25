

#include "raft/i_server.hpp"
#include "raft/i_state_machine.hpp"
#include <abb/http/http_server.hpp>
#include <abb/http/http_const.hpp>
#include <abb/http/http_request.hpp>
#include <abb/http/http_responce.hpp>
#include <abb/http/http_responce_writer.hpp>

#include <abb/base/log.hpp>
#include "raft/commond.hpp"
#include "raft/messages.hpp"
#include <abb/base/thread.hpp>
#include <map>
#include <string>
#include <vector>

raft::IServer* svr;

static void Split(const std::string&str,const std::string& sep,std::vector<std::string>& s_arr){
	int pre_pos = 0;
	while(true){
		std::string::size_type pos = str.find(sep,pre_pos);
		if( pos != std::string::npos ){
			pre_pos = pos+1;
			s_arr.push_back(str.substr(0,pos));
		}else{
			s_arr.push_back(str.substr(pre_pos));
			break;
		}
	}
}
class Store{
public:
	Store(){}
	~Store(){}
	void Set(const std::string& key,const std::string& val){
		abb::Mutex::Locker l(mtx_);
		map_[key] = val;
	}
	bool Get(const std::string& key,std::string& val){
		abb::Mutex::Locker l(mtx_);
		SS_MAP::iterator iter = map_.find(key);
		if(iter == map_.end()){
			return false;
		}
		val = iter->second;
		return true;
	}
private:
	typedef std::map<std::string, std::string> SS_MAP;
	SS_MAP map_;
	abb::Mutex mtx_;
};
Store store_;
class MyJoinCmd:public raft::Commond{
public:
	MyJoinCmd(){}
	virtual ~MyJoinCmd(){}
	virtual const char* CommondName(){
		return "MyJoinCmd";
	}
	virtual void Apply(raft::IServer*,std::string *save_error,raft::IMessage**ret){

	}
	virtual bool Encode(abb::Buffer& buf){return true;};
	virtual bool Decode(abb::Buffer& buf){return true;};
};
class SetCommond:public raft::Commond{
public:
	SetCommond(){}
	virtual ~SetCommond(){}
	virtual const char* CommondName(){
		return "SetCommond";
	}
	virtual void Apply(raft::IServer*,std::string *save_error,raft::IMessage**ret){
		store_.Set(key,val);
		if(ret){
			* ret = new raft::BoolMessage(true);
		}
	}
	virtual bool Encode(abb::Buffer& buf){
		buf << key << val;
		return true;
	};
	virtual bool Decode(abb::Buffer& buf){
		buf >> key >> val;
		return true;
	};
public:
	std::string key;
	std::string val;
};

class StateMachine:public raft::IStateMachine ,public abb::http::Server::Listener ,public raft::IFactory{
public:
	virtual ~StateMachine(){}
	virtual bool Save(abb::Buffer& buf){
		return true;
	}
	virtual bool Recovery(abb::Buffer& buf){
		return true;
	}
	virtual void HandleRequest(abb::http::Request* req,abb::http::ResponceWriter* rspw){
		std::string path = req->GetURL().Path;
		if(path == "/set"){
			std::string query = req->GetURL().RawQuery;
			std::vector<std::string> arr;
			std::string key ,value;
			Split(query,"&",arr);
			for(unsigned i =0;i<arr.size();i++){
				std::string::size_type pos = arr[i].find("=");
				if(pos != std::string::npos){
					if(arr[i].substr(0,pos) == "key"){
						key = arr[i].substr(pos+1);
					}
					if(arr[i].substr(0,pos) == "value"){
						value = arr[i].substr(pos+1);
					}
				}
			}
			SetCommond * set = new SetCommond();
			set->key = key;
			set->val = value;
			raft::BoolMessage rsp;
			std::string err;
			if( svr->ApplyCommond(set,rsp,err) ){
				rspw->GetResponce().SetStatusCode(abb::http::code::StatusOK);
				rspw->GetResponce().GetHeader().Set(abb::http::header::CONTENT_TYPE,"text/plain");
				std::string suc = "success";
				rspw->GetResponce().Body().Write((const void*)suc.c_str(),suc.size());
				rspw->Flush();
			}else{
				rspw->GetResponce().SetStatusCode(abb::http::code::StatusOK);
				rspw->GetResponce().GetHeader().Set(abb::http::header::CONTENT_TYPE,"text/plain");
				rspw->GetResponce().Body().Write((const void*)err.c_str(),err.size());
				rspw->Flush();
			}
		}else if(path == "/get"){
			
		}
		

	}
	virtual raft::Commond* GetJoinCmd(){
		return new MyJoinCmd();
	}
};

//mkdir name1 name2 name3
StateMachine st;
int main(int argc,const char* argv[]){
	raft::RegisterCommand("MyJoinCmd",raft::T_CommondCreator<MyJoinCmd>);
	raft::RegisterCommand("SetCommond",raft::T_CommondCreator<SetCommond>);
	abb::g_min_log_level = abb::LOGLEVEL_DEBUG;
	std::string addr1 = "localhost:5303";
	std::string addr2 = "localhost:5403";
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

