
#include "peer.hpp"
#include "raft/raft_server.hpp"
#include "raft_log.hpp"
#include <abb/base/log.hpp>
#define MAX_ENTRY_ONCE 200
namespace raft {

Peer::Peer(RaftServer* svr,
		const std::string& name,
		const std::string& addr)
:pre_log_index_(0),
 svr_(svr),
 name_(name),
 addr_(addr),
 bflush_(false),
 bstop_(false),
 pre_times(0){
}

Peer::~Peer() {
}
void Peer::StartHeartbead(){
	bstop_ = false;
	thread_.Start(Peer::ThreadMain,this);
}
void Peer::StopHeartbead(bool bflush){
	bflush_ = bflush;
	bstop_ = true;
	notify_.Notify();
	LOG(DEBUG) <<  this->name_ <<"StopHeartbead";
	thread_.Wait();
}
void  Peer::Loop(){

	while(!notify_.WaitTimeout(this->svr_->HeartbeatTimeout()) && !bstop_){
		this->Flush();
	}
	LOG(DEBUG) <<  this->name_ <<"Loop Exit";
	if(bflush_){
		Flush();
	}
}
void Peer::Flush(){
	uint64_t term;
	RaftLog::LogEntryArray arr;
	uint64_t start_index;
	this->svr_->GetLog()->GetStartInfo(&start_index,NULL);
	if(start_index > pre_log_index_){
		Snapshot* sna = this->svr_->GetSnapshot();
		if(sna){
			LOG(DEBUG) << "peer.snapshot.lastIndex=" << sna->LastIndex << ".lastTerm" << sna->LastTerm;
			SnapshotRequest req(this->svr_->Name(),*sna);
			this->SendSnapshotRequest(req);
			return;
		}
	}else{
		this->svr_->GetLog()->GetEntriesAfter(MAX_ENTRY_ONCE,this->pre_log_index_,&arr,&term);
		if(arr.size() > 0){
			AppendEntriesRequest req(this->svr_->Term(),
					pre_log_index_,
					term,
					svr_->GetLog()->GetCommitIndex(),
					this->svr_->Name(),
					arr);
			this->SendAppendEntriesRequest(req);
			return;
		}
	}
	AppendEntriesRequest req(this->svr_->Term(),
			pre_log_index_,
			term,
			svr_->GetLog()->GetCommitIndex(),
			this->svr_->Name());
	this->SendAppendEntriesRequest(req);

}
VoteResponce* Peer::SendVoteRequest(VoteRequest&req){
	VoteResponce* rsp = new VoteResponce();
	if( this->svr_->Transporter()->SendMessage(svr_,this->addr_,req,rsp) ){
		return rsp;
	}else{
		rsp->UnRef();
		return NULL;
	}
}
void Peer::SendSnapshotRequest(SnapshotRequest&req){
	LOG(DEBUG)<< "peer.snap.send: "<<this->name_;
	SnapshotResponce* rsp = new VoteResponce();
	if( ! this->svr_->Transporter()->SendMessage(svr_,this->addr_,req,rsp) ){
		rsp->UnRef();
		rsp = NULL;
	}
	if(!rsp){
		pre_times++;
		LOG(DEBUG)<< "peer.snap.timeout: " << this->svr_->Name()<< "->" << this->name_;
		return;
	}
	pre_times = 0;
	LOG(DEBUG)<< "peer.snap.recv: "<<this->name_;

	// If successful, the peer should have been to snapshot state
	// Send it the snapshot!
	if(rsp->success){
		this->SendSnapshotRecoveryRequest();
	}else{
		LOG(DEBUG)<< "peer.snap.failed: "<<this->name_;
	}
	rsp->UnRef();

}
void Peer::SendSnapshotRecoveryRequest(){
	SnapshotRecoveryRequest req(this->svr_->Name(),*this->svr_->GetSnapshot());
	LOG(DEBUG)<< "peer.snap.recovery.send: "<<this->name_;
	SnapshotRecoveryResponce* rsp = new SnapshotRecoveryResponce();
	if( ! this->svr_->Transporter()->SendMessage(svr_,this->addr_,req,rsp) ){
		rsp->UnRef();
		rsp = NULL;
	}
	if(!rsp){
		LOG(DEBUG)<< "peer.snap.recovery.timeout: " << this->svr_->Name()<< "->" << this->name_;
		return;
	}
	if (rsp->Success) {
		this->pre_log_index_ = req.LastIndex;
		LOG(DEBUG)<< "peer.snap.recovery.success: "<<this->name_ << "index:" << this->pre_log_index_;
	} else {
		LOG(DEBUG)<< "peer.snap.recovery.failed: " << this->svr_->Name()<< "->" << this->name_;
		return;
	}

	this->svr_->OnSnapshotRecoveryResponce(rsp);
	rsp->UnRef();
}
void Peer::SendAppendEntriesRequest(AppendEntriesRequest&req){
	LOG(TRACE)<< "peer.append.entries.send: "<<this->name_;
	AppendEntriesResponce* rsp = new AppendEntriesResponce();
	if( ! this->svr_->Transporter()->SendMessage(svr_,this->addr_,req,rsp) ){
		rsp->UnRef();
		rsp = NULL;
	}
	if(!rsp){
		pre_times++;
		LOG(DEBUG)<< "peer.append.timeout: " << this->svr_->Name()<< "->" << this->name_;
		return;
	}
	pre_times = 0;
	LOG(TRACE)<<"peer.append.resp: "<< this->svr_->Name()<< "<-" << this->GetName();
	mtx_.Lock();
	if(rsp->Success){
		if(req.Entries.size() > 0){
			this->pre_log_index_ = req.Entries.back()->index_;
			if(req.Entries.back()->term_ == this->svr_->Term()){
				rsp->append = true;
			}
		}
		LOG(TRACE) << "peer.append.resp.success: " <<  this->svr_->Name() <<  "; idx =" << this->pre_log_index_;
	}else{
		if(rsp->CommitIndex >= this->pre_log_index_){
			this->pre_log_index_ = rsp->CommitIndex;
			LOG(DEBUG) << "peer.append.resp.update: "<< this->name_<< "; idx ="<< pre_log_index_;
		}else if( pre_log_index_> 0) {
			pre_log_index_--;
			if (pre_log_index_ > rsp->Index) {
				pre_log_index_ = rsp->Index;
			}
			LOG(DEBUG) << "peer.append.resp.decrement: "<< this->name_ << "; idx ="<< this->pre_log_index_;
		}
	}
	mtx_.UnLock();
	rsp->peer = this->name_;
	this->svr_->OnAppendEntriesResponce(rsp);
	rsp->UnRef();
}

} /* namespace raft */
