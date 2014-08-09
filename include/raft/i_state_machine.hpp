
#ifndef __RAFT_I_STATE_MACHINE_HPP__
#define __RAFT_I_STATE_MACHINE_HPP__
#include <abb/base/buffer.hpp>
namespace raft {

class IStateMachine {
public:
	virtual ~IStateMachine(){}
	virtual bool Save(abb::Buffer& buf) = 0;
	virtual bool Recovery(abb::Buffer& buf) =0;
};

} /* namespace adcloud */

#endif /* I_STATE_MACHINE_HPP_ */
