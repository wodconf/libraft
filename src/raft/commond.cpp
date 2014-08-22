
#include "raft/commond.hpp"
#include <map>
#include <abb/base/log.hpp>
namespace raft {
const char* const Commond::TYPE_NAME = "Commond";
typedef std::map<std::string,common_factory_fn> CommonFactoryMap;
typedef CommonFactoryMap::iterator ITER;
static CommonFactoryMap cmd_fac_map_;
void RegisterCommand(const std::string& name,common_factory_fn fn){
	LOG(DEBUG) << "RegisterCommand<" << name << ">";
	cmd_fac_map_[name] = fn;
}
bool CommondRegisted(const std::string& name){
	return cmd_fac_map_.find(name) != cmd_fac_map_.end();
}
Commond* NewCommond(const std::string& name){
	ITER iter = cmd_fac_map_.find(name);
	if(iter == cmd_fac_map_.end()){
		LOG(WARN) << "create.not.regist.cmd   " << name;
		return NULL;
	}
	else return iter->second();
}
}
