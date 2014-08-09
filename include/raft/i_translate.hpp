
#ifndef __RAFT_I_TRANSLATE_HPP__
#define __RAFT_I_TRANSLATE_HPP__

#include "i_message.hpp"

namespace raft{

class Server;
class Peer;
class ITranslate{
public:
	virtual ~ITranslate(){}
	virtual bool SendMessage(Server* raft,const std::string& addr,IMessage&req,IMessage*rsp);
};

}


#endif /* I_TRANSLATE_HPP_ */
