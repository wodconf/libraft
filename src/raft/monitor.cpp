#include "monitor.hpp"
#include "server.hpp"
#include <abb/base/log.hpp>
namespace raft{


Monitor::Monitor(Server* svr)
:svr_(svr),bstop_(false){

}

Monitor::~Monitor(){

}
void Monitor::Start(){
	last_commit_index_ = this->svr_->CommitIndex();
	thread_monitor_.Start(ThreadMonitor,this);
}
void Monitor::Stop(){
	bstop_ = true;
	notify_.Notify();
	thread_monitor_.Wait();
}
void Monitor::Loop(){
	LOG(INFO) << "monitor.service.started";
	while(!bstop_){
		if( !this->notify_.WaitTimeout(this->svr_->GetConfig().GetSnapShotCheckInterval()) ){
			this->MonitorSnapshot();
		}else{
			LOG(DEBUG) << " monitor.exiting ";
		}
	}
}

void Monitor::MonitorSnapshot(){
	uint64_t currentIndex = this->svr_->CommitIndex();
	uint64_t	count = currentIndex - last_commit_index_;
	if(count > this->svr_->GetConfig().GetSnapShotNum()){
		std::string err;
		if( !this->svr_->StartTakeSnapshot(err) ){
			LOG(WARN) << "take.snapshot.fail   " << err;
		}else{
			LOG(WARN) << "take.snapshot.success  [" << currentIndex << ":" << last_commit_index_ << "]";
		}
		last_commit_index_ = currentIndex;
	}
}

}
