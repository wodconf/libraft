
#ifndef ADCLOUD_RAFT_I_TRANSLATE_HPP_
#define ADCLOUD_RAFT_I_TRANSLATE_HPP_

#include "i_message.hpp"

namespace raft{

class RaftServer;
class Peer;
class ITranslate{
public:
	virtual ~ITranslate(){}
	virtual bool SendMessage(RaftServer* raft,const std::string& addr,IMessage&req,IMessage*rsp);
};

}


#endif /* I_TRANSLATE_HPP_ */
