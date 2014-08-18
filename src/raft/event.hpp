
#ifndef AD_CLOUD_RAFT_EVENT_HPP_
#define AD_CLOUD_RAFT_EVENT_HPP_

#include <abb/base/ref_object.hpp>
#include <abb/base/thread.hpp>
#include "raft/i_message.hpp"
#include <abb/base/log.hpp>
namespace raft {
class EventBase:public abb::RefObject {
public:
	EventBase(IMessage* r):req(r){
		req->Ref();
	}
	~EventBase(){
		req->UnRef();
	}
	virtual void Notify(IMessage* msg,const std::string& error){
		if(msg)msg->UnRef();
	};
	IMessage* Request(){return req;}
private:
	IMessage* req;
	abb::Notification notify_;
};
class NotifyEvent:public EventBase {
public:
	NotifyEvent(IMessage* r):EventBase(r),rsp(NULL){
	}
	~NotifyEvent(){
	}
	void Wait(){
		notify_.Wait();
	}
	virtual void Notify(IMessage* msg,const std::string& error){
		rsp = msg;
		err = error;
		notify_.Notify();
	}
	IMessage* rsp;
	std::string err;
private:
	abb::Notification notify_;
};
typedef void EventCallback(void* arg,IMessage* back,const std::string& error);
class CallBackEvent:public EventBase{
	public:
	CallBackEvent(IMessage* r,EventCallback* callback,void* arg)
	:EventBase(r),callback_(callback),arg_(arg){
	}
	~CallBackEvent(){
	}
	virtual void Notify(IMessage* rsp,const std::string& error){
		callback_(arg_,rsp,error);
	}
private:
	EventCallback* callback_;
	void* arg_;
};

} /* namespace raft */

#endif /* EVENT_HPP_ */
