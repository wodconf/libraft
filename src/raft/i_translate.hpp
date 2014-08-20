
#ifndef __RAFT_I_TRANSLATE_HPP__
#define __RAFT_I_TRANSLATE_HPP__


#include <string>

namespace raft{

class IServer;
class IMessage;
class Commond;
class ITranslate{
public:
	virtual ~ITranslate(){}
	virtual bool SendMessage(IServer* raft,const std::string& addr,IMessage&req,IMessage&rsp,std::string& error) = 0;
	virtual bool SendCommond(IServer* raft,const std::string& addr,Commond&req,IMessage&rsp,std::string& error) = 0;
};

}


#endif /* I_TRANSLATE_HPP_ */
