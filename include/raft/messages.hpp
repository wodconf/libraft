#ifndef __RAFT_MESSAGES_HPP__
#define __RAFT_MESSAGES_HPP__

#include "i_message.hpp"

namespace raft{
class BoolMessage:public IMessage{
public:
	static const char* const TYPE_NAME;
	BoolMessage(bool suc);
	BoolMessage();
	virtual ~BoolMessage();
	virtual const char* TypeName(){return TYPE_NAME;}
	virtual bool Encode(abb::Buffer& buf);
	virtual bool Decode(abb::Buffer& buf);
public:
	bool success;
};
}

#endif
