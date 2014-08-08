/*
 * i_message.hpp
 *
 *  Created on: 2014-8-8
 *      Author: wd
 */

#ifndef I_MESSAGE_HPP_
#define I_MESSAGE_HPP_


#include <abb/base/ref_object.hpp>
#include <abb/base/buffer.hpp>

namespace raft{
class IMessage:public abb::RefObject{
public:
	virtual ~IMessage(){}
	virtual const char* TypeName() = 0;
	virtual bool Encode(abb::Buffer& buf){return true;};
	virtual bool Decode(abb::Buffer& buf){return true;};
};
}




#endif /* I_MESSAGE_HPP_ */
