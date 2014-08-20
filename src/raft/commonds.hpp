
#ifndef __RAFT_COMMONDS_HPP__
#define __RAFT_COMMONDS_HPP__

#include "raft/commond.hpp"

namespace raft {


class JoinCommond:public Commond {
public:
	static const char* const CMD_NAME;
public:
	JoinCommond():cmd_(NULL){}
	JoinCommond(const std::string& name,const std::string& addr,Commond* cmd):name_(name),addr_(addr),cmd_(cmd){}
	virtual ~JoinCommond(){}
	virtual const char* CommondName(){return CMD_NAME;}
	const std::string& NodeName(){
		return name_;
	}
	virtual void Apply(IServer*,std::string *save_error,IMessage**ret);
	virtual bool Encode(abb::Buffer& buf);
	virtual bool Decode(abb::Buffer& buf);
private:
	std::string name_;
	std::string addr_;
	Commond* cmd_;
};


}

#endif /* JOIN_COMMOND_HPP_ */
