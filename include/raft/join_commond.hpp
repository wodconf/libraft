
#ifndef JOIN_COMMOND_HPP_
#define JOIN_COMMOND_HPP_

#include "commond.hpp"

namespace raft {


class JoinCommond:public Commond {
public:
	static const char* const CMD_NAME = "JoinCommond" ;
public:
	JoinCommond(){}
	virtual ~JoinCommond(){}
	virtual const char* CommondName(){return CMD_NAME;}
	virtual const std::string& NodeName() = 0;
};


}

#endif /* JOIN_COMMOND_HPP_ */
