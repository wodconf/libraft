
#ifndef AD_CLOUD_RAFT_EVENT_HPP_
#define AD_CLOUD_RAFT_EVENT_HPP_

#include <abb/base/ref_object.hpp>
#include <abb/base/thread.hpp>
#include "raft/i_message.hpp"

namespace raft {

struct Event:public abb::RefObject {
	Event(IMessage* r):req(r),rsp(NULL){
		req->Ref();
	}
	~Event(){
		req->UnRef();
	}
	void Wait(){
		notify_.Wait();
	}
	void Notify(){
		notify_.Notify();
	}
	IMessage* Request(){return req;}
	void* rsp;
	std::string err;
private:
	IMessage* req;
	abb::Notification notify_;
};

} /* namespace raft */

#endif /* EVENT_HPP_ */
