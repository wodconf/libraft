
#ifndef AD_CLOUD_RAFT_EVENT_HPP_
#define AD_CLOUD_RAFT_EVENT_HPP_

#include <abb/base/ref_object.hpp>
#include <abb/base/thread.hpp>
#include "raft/i_message.hpp"
#include "raft/commond.hpp"
#include <abb/base/log.hpp>
#include <abb/base/date.hpp>
namespace raft {
class EventBase:public abb::RefObject {
public:
	EventBase(IMessage* r):req(r),create_time_(abb::Date::Now()){
		req->Ref();
	}
	~EventBase(){
		req->UnRef();
	}
	void Notify(IMessage* msg,const std::string& error){
		this->SetEnd();
		std::string name;
		if(this->Request()->TypeName() == Commond::TYPE_NAME){
			name = static_cast<Commond*>(this->Request())->CommondName();
		}else{
			name = this->Request()->TypeName();
		}
		uint64_t wait_time = this->StartTime() - this->CreateTime();
		uint64_t endtime = this->EndTime() - this->StartTime();
		if(wait_time > 10*1000 || endtime > 10*1000){
			LOG(DEBUG) << name << " process.wait.time " << wait_time << " process.deal.time " << endtime;
		}
		this->RealNotify(msg,error);
	};
	IMessage* Request() const{return req;}
	uint64_t StartTime() const {return start_time_.MicroSecond();}
	uint64_t CreateTime() const {return create_time_.MicroSecond();}
	uint64_t EndTime() const {return end_time_.MicroSecond();}
	void SetStart(){start_time_ = abb::Date::Now();}
private:
	void SetEnd(){
		end_time_ =abb::Date::Now();
	}
	virtual void RealNotify(IMessage* msg,const std::string& error){
		if(msg)msg->UnRef();
	}
private:
	IMessage* req;
	abb::Date create_time_;
	abb::Date start_time_;
	abb::Date end_time_;
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
	IMessage* rsp;
	std::string err;
private:
	virtual void RealNotify(IMessage* msg,const std::string& error){
		rsp = msg;
		err = error;
		notify_.Notify();
	}
	abb::Notification notify_;
};
typedef void EventCallback(void* arg,const EventBase* event_base,IMessage* back,const std::string& error);
class CallBackEvent:public EventBase{
	public:
	CallBackEvent(IMessage* r,EventCallback* callback,void* arg)
	:EventBase(r),callback_(callback),arg_(arg){
	}
	~CallBackEvent(){
	}
private:
	virtual void RealNotify(IMessage* rsp,const std::string& error){
		callback_(arg_,this,rsp,error);
	}
	EventCallback* callback_;
	void* arg_;
	
};

} /* namespace raft */

#endif /* EVENT_HPP_ */
