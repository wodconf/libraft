
#ifndef __RAFT_COMMOND_HPP__
#define __RAFT_COMMOND_HPP__

#include <map>

#include <string>
#include "i_message.hpp"
namespace raft {
class IServer;
class Commond;
typedef Commond* (*common_factory_fn)();
extern void RegisterCommand(const std::string& name,common_factory_fn fn);
extern Commond* NewCommond(const std::string& name);

template <class T>
Commond* T_CommondCreator(){
	return new T();
}

class Commond :public IMessage{
public:
	static const char* const TYPE_NAME;
public:
	virtual ~Commond(){}
	virtual const char* TypeName(){
		return TYPE_NAME;
	}
	virtual const char* CommondName() = 0;
	virtual void Apply(IServer*,std::string *save_error,IMessage**ret) = 0;
};

}

#endif /* I_COMMOND_HPP_ */
