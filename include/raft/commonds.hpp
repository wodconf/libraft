
#ifndef __RAFT_COMMONDS_HPP__
#define __RAFT_COMMONDS_HPP__

#include "commond.hpp"

namespace raft {


class JoinCommond:public Commond {
public:
	static const char* const CMD_NAME;
public:
	JoinCommond(){}
	virtual ~JoinCommond(){}
	virtual const char* CommondName(){return CMD_NAME;}
	virtual const std::string& NodeName() = 0;
};


}

#endif /* JOIN_COMMOND_HPP_ */
