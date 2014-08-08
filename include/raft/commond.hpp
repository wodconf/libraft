
#ifndef AD_CLOUD_RAFT_COMMOND_HPP_
#define AD_CLOUD_RAFT_COMMOND_HPP_

#include <map>

#include <string>
#include "i_message.hpp"
namespace raft {
class RaftServer;
class Commond;
typedef Commond* (*common_factory_fn)();
extern void RegisterCommand(const std::string& name,common_factory_fn fn);
extern Commond* NewCommond(const std::string& name);
class Commond :public IMessage{
public:
	static const char* const TYPE_NAME = "Commond";
public:
	Commond();
	virtual ~Commond();
	virtual const char* TypeName(){
		return TYPE_NAME;
	}
	virtual const char* CommondName() = 0;
	void Apply(RaftServer*,std::string *save_error,void**ret) = 0;
};

}

#endif /* I_COMMOND_HPP_ */
