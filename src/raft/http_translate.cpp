#include "http_translate.hpp"
#include "raft/commond.hpp"
#include "raft/i_message.hpp"
#include <abb/http/http_client.hpp>
#include <abb/http/http_const.hpp>
#include <abb/http/http_responce.hpp>
#include <string.h>
#include <abb/base/log.hpp>
#include <abb/base/date.hpp>


using namespace raft;
HttpTranslate::HttpTranslate(){

}
bool HttpTranslate::SendByHttpUrl(IServer* raft,const std::string& url,IMessage&snd,IMessage&rcv,std::string& error,int timeout){
	using namespace abb::http;
	Request req(method::POST,version::HTTP_1_1);
	if(!req.SetUrl(url)){
		error = "addr error";
		return false;
	}
	if(!snd.Encode(req.Body())){
		error = "message encode fail";
		return false;
	}
	int err = 0;
	Responce*rsp = SyncDo(req,&err,timeout);
	if(!rsp){
		error = strerror(err);
		LOG(WARN) << "url= " << url << " error:" << error;
		return false;
	} 
	if(rsp->GetStatusCode() == code::StatusOK){
		bool ok = rcv.Decode(rsp->Body()); 
		if(rsp){
			rsp->UnRef();
		}
		return ok;
	}else if(rsp->GetStatusCode() == code::StatusInternalServerError){
		error = std::string((const char*)rsp->Body().ReadPtr(),rsp->Body().ReadSize());
		LOG(INFO) << "rsp code" <<rsp->GetStatusCode() << error << " url = " << url;
		return false;
	}else if( rsp->GetStatusCode() == code::StatusMethodNotAllowed){
		error = "bad.method";
		return false;
	}else{
		error = "unknow.error";
		return false;
	}
}
bool HttpTranslate::SendMessage(raft::IServer* raft,const std::string& addr,raft::IMessage& snd,raft::IMessage&rcv,std::string& error){
	std::string url = "http://"+addr+"/"+snd.TypeName();
	uint64_t now = abb::Date::Now().MilliSecond();
	bool ret = SendByHttpUrl(raft,url,snd,rcv,error,10000);
	uint64_t df = abb::Date::Now().MilliSecond() - now;
	if(df > 20){
		LOG(INFO) << "send.message.df.large df = " << df << " url = " << url;
	}
	return ret;
}
bool HttpTranslate::SendCommond(IServer* raft,const std::string& addr,Commond& cmd,IMessage&rcv,std::string& error){
	std::string url = "http://"+addr+"/"+cmd.TypeName() + "/" +cmd.CommondName();
	uint64_t now = abb::Date::Now().MilliSecond();
	bool ret =SendByHttpUrl(raft,url,cmd,rcv,error,3000);
	uint64_t df = abb::Date::Now().MilliSecond() - now;
	if(df > 100){
		LOG(INFO) << "send.commond.df.large df = " << df << " url = " << url;
	}
	return ret;
}
