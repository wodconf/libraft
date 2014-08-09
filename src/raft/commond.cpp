
#include "raft/commond.hpp"
#include <map>
namespace raft {
const char* const Commond::TYPE_NAME = "Commond";
typedef std::map<std::string,common_factory_fn> CommonFactoryMap;
typedef CommonFactoryMap::iterator ITER;
static CommonFactoryMap cmd_fac_map_;
void RegisterCommand(const std::string& name,common_factory_fn fn){
	cmd_fac_map_[name] = fn;
}
Commond* NewCommond(const std::string& name){
	ITER iter = cmd_fac_map_.find(name);
	if(iter == cmd_fac_map_.end()) return NULL;
	else return iter->second();
}
}
