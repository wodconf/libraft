#ifndef __RAFT_HTTP_TRANSLATE_HPP__
#define __RAFT_HTTP_TRANSLATE_HPP__


#include "raft/i_translate.hpp"
namespace raft{
	class HttpTranslate:public ITranslate{
	public:
		HttpTranslate();
		virtual ~HttpTranslate(){}
		virtual bool SendMessage(IServer* raft,const std::string& addr,IMessage& snd,IMessage& rcv,std::string& error);
		virtual bool SendCommond(IServer* raft,const std::string& addr,Commond& snd,IMessage& rcv,std::string& error);
	private:
		bool SendByHttpUrl(IServer* raft,const std::string& url,IMessage& snd,IMessage& rcv,std::string& error,int timeout);
	};
}



#endif


