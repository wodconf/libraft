


#include "raft/messages.hpp"
#include <abb/base/log.hpp>

namespace raft{
const char* const BoolMessage::TYPE_NAME = "BoolMessage";;
BoolMessage::BoolMessage():success(false){

}
BoolMessage::BoolMessage(bool suc):success(suc){

}
BoolMessage::~BoolMessage(){

}
bool BoolMessage::Encode(abb::Buffer& buf){
	buf << success;
	return true;
};
bool BoolMessage::Decode(abb::Buffer& buf){
	buf >> success;
	return true;
};
}
