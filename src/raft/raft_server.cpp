

#include "raft_server.hpp"
#include <abb/base/log.hpp>
#include "event.hpp"
#include "join_commond.hpp"
#include "peer.hpp"
#include "raft_log.hpp"
#include <algorithm>
#include <sys/types.h>
#include <dirent.h>
#include <sstream>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#define NumberOfLogEntriesAfterSnapshot 100
namespace raft {

static uint64_t MSNow()
{
	struct timeval time;
	gettimeofday(&time,NULL);
	uint64_t tmp =  time.tv_sec;
	tmp*=1000;
	tmp+=time.tv_usec/1000;
	return tmp;
}

class VoteRequestRun:public abb::CallBack{
public:
	VoteRequestRun(RaftServer* svr,
			VoteRequest* req,
			Peer* peer):peer(peer),req(req),svr(svr){
		req->Ref();
		peer->Ref();
	}
	virtual ~VoteRequestRun(){
		req->UnRef();
		peer->UnRef();
	}
	virtual void Call(){
		LOG(DEBUG) << "SendVoteRequest   " << svr->Name() <<" ->"<< peer->GetName() << " at.term " << req->Term;
		VoteResponce* rsp = peer->SendVoteRequest(*req);
		if(rsp){
			rsp->snd_term = req->Term;
			Event* ev = new Event(rsp);
			svr->sa_chan_.Push(ev);
			rsp->UnRef();
		}
		delete this;
	}
	VoteRequest* req;
	Peer* peer;
	RaftServer* svr;
};
class StopRequest:public IMessage{
public:
	static const char*const TYPE_NAME = "StopRequest" ;
public:
	StopRequest(){};
	virtual  ~StopRequest(){}
	virtual const char* TypeName(){return TYPE_NAME;}
};
RaftServer::RaftServer(const std::string& name,
		const std::string& path,
		ITranslate* t,
		const std::string& connectiongString,
		void* ctx,
		IStateMachine* machine)
:name_(name),
 itranslate_(t),
 addr_(connectiongString),
 context_(ctx),
 path_(path),
 sa_chan_(4096),
 pending_snapshot_(NULL),
 snapshot_(NULL),
 state_machine_(machine)
{
	cur_term_ = 0;
	this->log_ = new RaftLog(this);
	state_ = STOPED;
	election_time_ = 400;
	heartbeat_time_ = 300;

}

RaftServer::~RaftServer() {
	this->log_->Close();
	delete this->log_;
}
bool RaftServer::GetLeaderAddr(std::string& addr,std::string* error){
	abb::Mutex::Locker l(mtx_);
	if(this->state_ == LEADER){
		addr = this->addr_;
		return true;
	}else if(!this->leader_.empty()){
		PeerMap::iterator iter = this->peermap_.find(leader_);
		if( iter != this->peermap_.end()){
			addr = iter->second->GetAddr();
			return true;
		}else{
			if(error) *error = "noleader peer";
			return false;
		}
	}else{
		if(error) *error = "noleader";
		return false;
	}
}
bool RaftServer::Promotable(){
	return this->log_->GetCurrentIndex() > 0;
}
bool RaftServer::IsLogEmpty(){
	return log_->IsEmpty();
}
uint64_t RaftServer::CommitIndex(){
	return log_->GetCommitIndex();
}
class PathSort{
public:
	bool operator()(const std::string& a,const std::string& b){
		std::istringstream sa(a);
		uint64_t a_term,a_index,b_term,b_index;
		sa >> a_term >> a_index;
		std::istringstream sb(b);
		sb >> b_term >> b_index;
		if(a_term == b_term){
			return a_index < b_index;
		}
		return a_term < b_term;
	}
};
bool RaftServer::LoadSnapshot(){
	std::string sp = this->path_  + "/snapshot/";
	LOG(INFO) << "load.snapshot.at = " << sp;
	DIR *dirptr = NULL;
	dirptr = opendir(sp.c_str());
	if(!dirptr){
		LOG(WARN) << "load.snapshot.fail " << strerror(errno);
		return false;
	}
	struct dirent *entry;
	std::vector<std::string> arr;
	while ( (entry = readdir(dirptr) ) != NULL){
		std::string item (entry->d_name);
		if(item == "." || item == "..") continue;
		arr.push_back(entry->d_name);
	}
	if(arr.size() == 0){
		LOG(WARN) << "LoadSnapshot no snapshot file dir:" << sp;
		return false;
	}
	PathSort les;
	std::sort(arr.begin(),arr.end(),les);
	std::string pa = arr.back();
	LOG(INFO) << "snapshot. " << sp+pa;
	Snapshot* na = Snapshot::Load(sp+pa);
	if(na){
		snapshot_ = na;
		if( ! this->state_machine_->Recovery(snapshot_->state) ){
			return false;
		}
		for(int i=0;i<na->Peers.size();i++){
			this->AddPeer(na->Peers[i].name,	na->Peers[i].addr);
		}
		LOG(DEBUG) << "load.snapshot.term=" << snapshot_->LastTerm << ".index=" <<snapshot_->LastIndex;
		this->log_->SetStartInfo(snapshot_->LastIndex,snapshot_->LastTerm);
		this->log_->UpdateCommitIndex(snapshot_->LastIndex);
		return true;
	}else{
		LOG(WARN) << "load.snapshot.fail " << "decode.fail " << sp+pa;
	}
	return false;
}
/*
 * if s.stateMachine == nil {
			return errors.New("Snapshot: Cannot create snapshot. Missing state machine.")
		}
 */
bool RaftServer::TakeSnapshot(std::string& save_error){

	if(this->pending_snapshot_){
		save_error = "Snapshot: Last snapshot is not finished.";
		return false;
	}
	LOG(DEBUG) << "take.snapshot";
	uint64_t lastIndex, lastTerm;
	uint64_t startIndex, startTerm;
	this->log_->GetCommitInfo(&lastIndex,&lastTerm);
	this->log_->GetStartInfo(&startIndex,&startTerm);
	if(lastIndex == startIndex){
		return true;
	}
	std::string path = this->GetSnapshotPath(lastIndex,lastTerm);

	pending_snapshot_ = new Snapshot();
	pending_snapshot_->LastIndex = lastIndex;
	pending_snapshot_->LastTerm = lastTerm;
	pending_snapshot_->Path = path;
	LOG(DEBUG) << "pending_snapshot.lastIndex=" << lastIndex << ".lastTerm" << lastTerm;
	{
		abb::Mutex::Locker l(mtx_);
		for(PeerMap::iterator iter = peermap_.begin();iter != peermap_.end();iter++){
			PeerInfo info;
			info.addr = iter->second->GetAddr();
			info.name = iter->second->GetName();
			LOG(DEBUG) << "pending_snapshot.peer " << info.name << " " << info.addr;
			pending_snapshot_->Peers.push_back(info);
		}
		PeerInfo info;
		info.addr = addr_;
		info.name = this->name_;
		LOG(DEBUG) << "pending_snapshot.peer " << info.name << " " << info.addr;
		pending_snapshot_->Peers.push_back(info);
	}
	this->state_machine_->Save(pending_snapshot_->state);

	this->SaveSnapshot();

	if(lastIndex - startIndex > NumberOfLogEntriesAfterSnapshot){
		uint64_t compactIndex = lastIndex - NumberOfLogEntriesAfterSnapshot;

		LogEntry* e = this->log_->GetLogEntry(compactIndex);
		if(e){
			uint64_t compactTerm = e->term_;
			this->log_->Compact(compactIndex,compactTerm);
		}
	}
	return true;
}
std::string RaftServer::GetSnapshotPath(uint64_t index,uint64_t term){
	std::ostringstream s;
	s << this->path_ << "/snapshot/" << term << "_" << index << ".ss";
	return s.str();
}
void* RaftServer::send(IMessage* req,std::string& save_error){
	if(!req){
		save_error = "ARGUMENT_ERROR";
		return NULL;
	}
	Event* ev = new Event(req);
	ev->Ref();
	this->sa_chan_.Push(ev);
	ev->Wait();
	save_error = ev->err;
	void* rsp = ev->rsp;
	ev->UnRef();
	return rsp;
}
bool RaftServer::FlushCommitIndex(){
	std::string file = path_ + "/conf.tmp";
	std::string o_file = path_ + "/conf";
	int fd = open(file.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
	if(fd < 0){
		LOG(WARN) << "Open Fial fail: path:" << file << "err:" << strerror(errno);
		return false;
	}
	lseek(fd,0,SEEK_SET);
	uint64_t index = this->log_->GetCommitIndex();
	LOG(INFO) << "flush.commit.index = " << index;
	int ret = write(fd,&index,sizeof(index));
	fsync(fd);
	close(fd);
	if(ret != sizeof(index)){
		LOG(WARN) << "write.fail" << file << "err:" << strerror(errno);
		return false;
	}
	rename(file.c_str(),o_file.c_str());
	return true;
}
bool RaftServer::ReadCommitIndex(){
	std::string file = path_ + "/conf";
	int fd = open(file.c_str(),O_RDONLY,0600);
	if(fd < 0){
		LOG(WARN) << "Open Fial fail: path:" << file << "err:" << strerror(errno);
		return true;
	}
	lseek(fd,0,SEEK_SET);
	uint64_t index;
	int ret = read(fd,&index,sizeof(index));
	close(fd);
	LOG(INFO) << "read.commit.index = " << index;
	if(ret < sizeof(index)){
		return true;
	}
	this->log_->UpdateCommitIndex(index);
	return true;
}
IMessage* ProcessMessage(IMessage* req,std::string& save_error){
	return static_cast<IMessage*>(this->send(req,save_error));
}
void* RaftServer::DealCommond(Commond* cmd,std::string& save_error){
	return this->send(cmd,save_error);
}
void RaftServer::OnAppendEntriesResponce(AppendEntriesResponce* req){
	Event* ev = new Event(req);
	this->sa_chan_.Push(ev);
}
void RaftServer::OnSnapshotRecoveryResponce(SnapshotRecoveryResponce* rsp){
	Event* ev = new Event(rsp);
	this->sa_chan_.Push(ev);
}
bool RaftServer::AddPeer( const std::string&name, const std::string& connectiongString){
	LOG(INFO) << "raft.addr.peer.name = " << name << " .addr = "<< connectiongString << " .state = " << GetStateString();
	abb::Mutex::Locker l(mtx_);
	PeerMap::iterator iter = this->peermap_.find(name);
	if(iter != this->peermap_.end()){
		FlushCommitIndex();
		return true;
	}
	if(this->name_ != name){
		Peer* peer = new Peer(this,name,connectiongString);
		this->peermap_[name] = peer;
		if(this->State() == LEADER){
			peer->StartHeartbead();
		}
	}
	FlushCommitIndex();
	return true;
}
bool RaftServer::RemovePeer( const std::string&name){
	LOG(INFO) << "raft.remove.peer.name = " << name << " .state = " << GetStateString();
	abb::Mutex::Locker l(mtx_);
	if(this->name_ != name){
		PeerMap::iterator iter = this->peermap_.find(name);
		if(iter == this->peermap_.end()){
			FlushCommitIndex();
			return false;
		}
		// Stop peer and remove it.
		if(this->State() == LEADER){
			iter->second->StopHeartbead(true);
		}
		iter->second->UnRef();
		this->peermap_.erase(iter);
	}
	FlushCommitIndex();
	return true;
}
bool RaftServer::Init(){
	if(Running()){
		LOG(DEBUG) << "Raft Is Running" << this->state_;
		return false;
	}
	if(this->State() == INITED){
		return true;
	}

	std::string sp = this->path_+"/snapshot";
	if( 0 > mkdir(sp.c_str(),0700)){
		if(errno != EEXIST){
			LOG(WARN) << "mkdir fail:" << sp << errno<< "err:"<< strerror(errno);
			return false;
		}
	}
	this->ReadCommitIndex();
	if( !this->log_->Open(this->path_+"/log") ){
		return false;
	}
	this->SetState(INITED);
	return true;
}
bool RaftServer::Start(){
	if(Running()){
		LOG(DEBUG) << "Raft Is Running" << this->state_;
		return false;
	}
	if(!Init()){
		return false;
	}
	this->SetState(FOLLOWER);
	this->log_->GetLastInfo(NULL,&this->cur_term_);
	thread_.Start(ThreadMain,this);
	this->pool.SetNumThread(4);
	this->pool.Start();
	return true;
}
void RaftServer::Stop(){
	if(!Running()){
		LOG(DEBUG) << "Raft Is NOt Running" << this->state_;
		return ;
	}
	StopRequest req;
	std::string err;
	this->send(&req,err);
}
void RaftServer::SetState(STATE s) {
	abb::Mutex::Locker l(mtx_);

	this->state_ = s;
	if (this->state_ == LEADER){
		this->leader_ = this->name_;
		this->sync_map_.clear();
	}

}
bool RaftServer::ProcessIfStopEvent(Event* ev){
	if(ev->req->TypeName() == StopRequest::TYPE_NAME){
		this->SetState(STOPED);
		ev->req = NULL;
		ev->notify_.Notify();
		return true;
	}
	return false;
}
bool RaftServer::ProcessIfSnapshotRequest(Event* ev){
	if( ev->req->TypeName() == SnapshotRequest::TYPE_NAME ){
		SnapshotRequest* req = static_cast<SnapshotRequest*>(ev->req);
		LogEntry* entry = this->log_->GetLogEntry(req->LastIndex);
		if(entry && entry->term_ == req->LastTerm){
			ev->rsp = new SnapshotResponce(false);
		}else{
			this->SetState(SNAPSHOT);
			ev->rsp = new SnapshotResponce(true);
		}
		ev->notify_.Notify();
		return true;
	}
	return false;
}
bool RaftServer::ProcessIfAppendEntriesRequest(Event* ev,bool* bupdate){
	if( ev->req->TypeName() == AppendEntriesRequest::TYPE_NAME ){
		ev->rsp = this->ProcessAppendEntriesRequest(static_cast<AppendEntriesRequest*>(ev->req),ev->err,bupdate);
		ev->notify_.Notify();
		return true;
	}
	return false;
}
bool RaftServer::ProcessIfAppendEntriesResponce(Event* ev){
	if(ev->req->TypeName() == AppendEntriesResponce::TYPE_NAME){
		this->ProcessAppendEntriesResponce(static_cast<AppendEntriesResponce*>(ev->req));
		return true;
	}
	return false;
}
bool RaftServer::ProcessIfSnapshotRecoveryRequest(Event* ev){
	if(ev->req->TypeName() == SnapshotRecoveryRequest::TYPE_NAME){
		ev->rsp = this->ProcessSnapshotRecoveryRequest(static_cast<SnapshotRecoveryRequest*>(ev->req),ev->err);
		ev->notify_.Notify();
		return true;
	}
	return false;
}
bool RaftServer::ProcessIfSnapshotRecoveryResponce(Event* ev){
	if(ev->req->TypeName() == SnapshotRecoveryResponce::TYPE_NAME){
		this->ProcessSnapshotRecoveryResponce(static_cast<SnapshotRecoveryResponce*>(ev->req));
		return true;
	}
	return false;
}
bool RaftServer::ProcessIfVoteRequest(Event* ev,bool* bupdate){
	if(ev->req->TypeName() == VoteRequest::TYPE_NAME){
		ev->rsp = this->ProcessVoteRequest(static_cast<VoteRequest*>(ev->req),ev->err,bupdate);
		ev->notify_.Notify();
		return true;
	}
	return false;
}
bool RaftServer::ProcessIfJoinCmd(Event* ev){
	if(ev->req->TypeName() == Commond::TYPE_NAME){
		Commond* cmd = static_cast<Commond*>(ev->req);
		if(cmd->CommondName() == JoinCommond::CMD_NAME){
			JoinCommond* cmd = static_cast<JoinCommond*>( ev->req);
			if(this->log_->GetCurrentIndex() == 0 && cmd->NodeName() == this->name_){
				LOG(DEBUG) << "selfjoin and promote to leader";
				this->SetState(LEADER);
				this->ProcessCommond(static_cast<Commond*>(ev->req),ev);
			}else{
				ev->err = "NotLeaderError";
				ev->notify_.Notify();
			}
			return true;
		}
	}
	return false;
}
bool RaftServer::ProcessIfVoteResponce(Event* ev,int*vote){
	if(ev->req->TypeName() == VoteResponce::TYPE_NAME){
		VoteResponce* rsp = static_cast<VoteResponce*>(ev->req);
		LOG(DEBUG) << "ProcessVoteResponce" << rsp->VoteGranted;
		if(this->cur_term_ != rsp->snd_term){
			LOG(WARN) << "responce.pre.vote";
		}else{
			if(rsp->VoteGranted){
				if(vote)(*vote)++;
			}else if (rsp->Term > this->cur_term_) {
				LOG(DEBUG)<< ("server.candidate.vote.failed");
				SetCurrentTerm(rsp->Term, "");
			}else{
				LOG(DEBUG)<< ("server.candidate.vote:denied");
			}
		}
		
		rsp->UnRef();
		return true;
	}
	return false;
}

bool RaftServer::ProcessIfCmd(Event*ev){
	if(ev->req->TypeName() == Commond::TYPE_NAME){
		this->ProcessCommond(static_cast<Commond*>(ev->req),ev);
		return true;
	}
	return false;
}
void RaftServer::Loop(){
	while(true){
		if(state_ == LEADER){
			this->LeaderLoop();
		}else if(state_ == FOLLOWER){
			FollowerLoop();
		}else if(state_ == CANDIDATE){
			CandidateLoop();
		}else if(state_ == STOPED){
			break;
		}else if(state_ == SNAPSHOT){
			SnapshotLoop();
		}else {
			LOG(WARN) << "raft_loop unknow state:" << state_;
		}
	}
}void RaftServer::SnapshotLoop(){
	LOG(INFO) << "I am SNAPSHOT";
	this->SetState(SNAPSHOT);
	while(this->State() == SNAPSHOT){
		Event* ev = this->sa_chan_.Poll();
		if(!ev || !ev->req){
			continue;
		}
		LOG(TRACE) << "SnapshotLoop process event:" << ev->req->TypeName();
		if(!ProcessIfStopEvent(ev)){
			if(!ProcessIfAppendEntriesRequest(ev,NULL)){
				if(!ProcessIfSnapshotRecoveryRequest(ev)){
					if(!ProcessIfVoteRequest(ev,NULL)){
						LOG(WARN) << "SnapshotLoop unknow commond" << ev->req->TypeName();
						ev->err = "NOTLEADERERROR";
						ev->notify_.Notify();
					}
				}
			}
		}
		ev->UnRef();
	}
}
void RaftServer::LeaderLoop(){
	LOG(INFO) << "I am Leader";
	this->SetState(LEADER);
	uint64_t log_index;
	this->log_->GetLastInfo(&log_index,NULL);
	for(PeerMap::iterator iter = peermap_.begin();iter != peermap_.end();iter++){
		iter->second->SetPreLogIndex(log_index);
		iter->second->StartHeartbead();
	}
	while(this->State() == LEADER){
		Event* ev = this->sa_chan_.Poll();
		if(!ev || !ev->req){
			continue;
		}
		LOG(TRACE) << "LeaderLoop process event:" << ev->req->TypeName();
		if(!ProcessIfStopEvent(ev)){
			if(!ProcessIfAppendEntriesRequest(ev,NULL)){
				if(!ProcessIfAppendEntriesResponce(ev)){
					if(!ProcessIfVoteRequest(ev,NULL)){
						if(!ProcessIfCmd(ev)){
							LOG(WARN) << "LeaderLoop unknow commond" << ev->req->TypeName();
							ev->err = "NOTLEADERERROR";
							ev->notify_.Notify();
						}
					}
				}
			}
		}
		ev->UnRef();
	}
	this->sync_map_.clear();
}
void RaftServer::FollowerLoop(){
	LOG(INFO) << "I am Follower";
	this->SetState(FOLLOWER);
	int e_timeout = this->ElectionTimeout();
	int timeout = e_timeout + rand()%e_timeout;
	while(this->State() == FOLLOWER){
		Event* ev = NULL;
		bool bupdate = false;
		int now = MSNow();
		if( !this->sa_chan_.PollTimeout(timeout,&ev) || timeout < 0 ){
			LOG(DEBUG) << "PollTimeout:"<< timeout << "time:" << (MSNow()-now);
			if(this->Promotable()){
				this->SetState(CANDIDATE);
				break;
			}else{
				bupdate = true;
			}
		}else{
			if(!ev || !ev->req){
				LOG(DEBUG) << "EMPTY";
				continue;
			}
			LOG(TRACE) << "FollowerLoop process event:" << ev->req->TypeName();
			if(!ProcessIfStopEvent(ev)){
				if(!ProcessIfAppendEntriesRequest(ev,&bupdate)){
					if(!ProcessIfAppendEntriesResponce(ev)){
						if(!ProcessIfVoteRequest(ev,&bupdate)){
							if(!ProcessIfSnapshotRequest(ev)){
								if(!ProcessIfJoinCmd(ev)){
									LOG(WARN) << "FLOOWER NOT DEAL THIS " <<ev->req->TypeName();
									ev->err = "NOTLEADERERROR";
									ev->notify_.Notify();
								}
							}
						}
					}
				}
			}
			ev->UnRef();
		}

		if(bupdate){
			timeout = e_timeout + rand()%e_timeout;
		}else{
			timeout -= (MSNow() - now);
		}
	}
}
void RaftServer::CandidateLoop(){
	LOG(INFO) << "I am Candidate";
	uint64_t last_index,last_term;
	this->log_->GetLastInfo(&last_index,&last_term);
	std::string preleader = this->leader_;
	this->leader_ = "";
	while(this->State() == CANDIDATE){
		this->cur_term_++;
		this->voted_for_ = this->name_;
		int e_timeout = this->ElectionTimeout();
		int timeout = e_timeout + rand()%e_timeout;
		int votesGranted = 1;
		LOG(INFO) << "start_vote" << this->cur_term_;
		VoteRequest* req = new VoteRequest(this->cur_term_,last_index,last_term,this->name_);
		{
			abb::Mutex::Locker l(mtx_);
			for(PeerMap::iterator iter = peermap_.begin();iter != peermap_.end();iter++){
				this->pool.Execute(new VoteRequestRun(this,req,iter->second));
			}
		}

		while(this->State() == CANDIDATE){
			if (votesGranted >= this->QuorumSize()) {
				LOG(INFO) << "vote.win" << this->name_;
				this->SetState(LEADER);
				break;
			}
			Event* ev;
			int now = MSNow();
			if( !this->sa_chan_.PollTimeout(timeout,&ev) ){
				break;
			}else{
				if(ev  && ev->req){
					LOG(TRACE) << "CandidateLoop process event:" << ev->req->TypeName();
					if(!ProcessIfStopEvent(ev)){
						if(!ProcessIfAppendEntriesRequest(ev,NULL)){
							if(!ProcessIfVoteRequest(ev,NULL)){
								if(!ProcessIfVoteResponce(ev,&votesGranted)){
									LOG(WARN) << "CANDIDATE NOT DEAL THIS " << ev->req->TypeName();
									ev->err = "NOTLEADERERROR";
									ev->notify_.Notify();
								}
							}
						}
					}
					ev->UnRef();
				}
			}
			timeout -= (MSNow() - now);
			if(timeout <= 0){
				break;
			}
		}
	}

}
int RaftServer::QuorumSize(){
	abb::Mutex::Locker l(mtx_);
	int size = peermap_.size()+1;
	return (size/2) +1;
}

void RaftServer::SetCurrentTerm(uint64_t term,const std::string& leaderName){
	mtx_.Lock();
	if(term > this->cur_term_){
		mtx_.UnLock();
		if(this->state_ == LEADER){
			for(PeerMap::iterator iter = peermap_.begin();iter != peermap_.end();iter++){
				iter->second->StopHeartbead(false);
			}
		}
		mtx_.Lock();
		this->state_ = FOLLOWER;
		if(this->leader_ != leaderName){
			LOG(INFO) << "[change.leader] " << this->leader_ << " to " << leaderName;
			this->leader_ = leaderName;
		}
		this->voted_for_ = "";
		this->cur_term_ = term;
		LOG(INFO) << "[update.current.term]  " << term;
		mtx_.UnLock();
	}else{
		mtx_.UnLock();
		LOG(DEBUG) << "SetCurrentTerm Fail" << term << leaderName << "cur_term_" << cur_term_;
	}

}
AppendEntriesResponce* RaftServer::ProcessAppendEntriesRequest(AppendEntriesRequest* req,std::string& save_error,bool* bupdate){
	if(req->term < this->cur_term_){
		if(bupdate)*bupdate = false;
		return new AppendEntriesResponce(cur_term_,this->log_->GetCurrentIndex(),false,this->log_->GetCommitIndex());
	}
	if(req->term == this->cur_term_){
		this->state_ = FOLLOWER;
		if(this->leader_ != req->LeaderName){
			LOG(INFO) << "[change.learder] " << this->leader_ << "->" << req->LeaderName;
			this->leader_ = req->LeaderName;
		}
	}else{
		SetCurrentTerm(req->term,req->LeaderName);
	}
	if( !this->log_->Truncate(req->PrevLogIndex,req->PrevLogTerm) ){
		if(bupdate)*bupdate = true;
		return new AppendEntriesResponce(cur_term_,this->log_->GetCurrentIndex(),false,this->log_->GetCommitIndex());
	}
	if(! this->log_->AppendEntries(req->Entries,NULL)){
		if(bupdate)*bupdate = true;
		return new AppendEntriesResponce(cur_term_,this->log_->GetCurrentIndex(),false,this->log_->GetCommitIndex());
	}
	if(!this->log_->CommitToIndex(req->CommitIndex)){
		if(bupdate)*bupdate = true;
		return new AppendEntriesResponce(cur_term_,this->log_->GetCurrentIndex(),false,this->log_->GetCommitIndex());
	}
	if(bupdate)*bupdate = true;
	return new AppendEntriesResponce(cur_term_,this->log_->GetCurrentIndex(),true,this->log_->GetCommitIndex());
}
VoteResponce* RaftServer::ProcessVoteRequest(VoteRequest* req,std::string& save_error,bool* bupdate){

	if(req->Term < this->cur_term_){
		LOG(DEBUG) << "vote.fail  term.small.vote.false " << req->CandidateName << " value"<< req->Term << ":" << this->cur_term_;
		if(bupdate)*bupdate = false;
		return new VoteResponce(cur_term_,false);
	}
	if(req->Term > this->cur_term_){
		SetCurrentTerm(req->Term,"");
	}else if(this->voted_for_ != "" && this->voted_for_ != req->CandidateName){
		LOG(DEBUG) << "vote.fail  has.vote.to." << voted_for_ << ".req.name" << req->CandidateName;
		if(bupdate)*bupdate = false;
		return new VoteResponce(cur_term_,false);
	}
	uint64_t last_index,last_term;
	this->log_->GetLastInfo(&last_index,&last_term);

	if(last_index > req->LastLogIndex || last_term > req->LastLogTerm){
		LOG(DEBUG) << "vote.fail  term.small.or.index.small.vote.fail" <<  req->CandidateName 
		<< " index " << last_index << ":" << req->LastLogIndex
		<< " term " << last_term << ":" << req->LastLogTerm;
		if(bupdate)*bupdate = false;
		return new VoteResponce(cur_term_,false);
	}
	LOG(DEBUG) << "vote.success  vote.to.req" << req->CandidateName;
	this->voted_for_ = req->CandidateName;
	if(bupdate)*bupdate = true;
	return new VoteResponce(cur_term_,true);
}
void RaftServer::ProcessCommond(Commond* req,Event* ev){
	LOG(DEBUG) << "server.command.process = " << req->CommondName();
	LogEntry* ent = log_->CreateEntry(this->cur_term_,req,ev);
	if(!ent){
		ev->err = "server.command.log.entry.error:creatfail";
		ev->notify_.Notify();
	}
	if(!this->log_->AppendEntry(ent,&ev->err)){
		LOG(DEBUG) << "server.command.process" << "AppendEntry fail";
		ev->notify_.Notify();
		ent->UnRef();
	}
	this->sync_map_[this->name_] = true;
	if(this->peermap_.size() == 0){
		uint64_t cindex = this->log_->GetCurrentIndex();
		this->log_->CommitToIndex(cindex);
		LOG(DEBUG) <<"commit index " << cindex;
	}
}
SnapshotRecoveryResponce* RaftServer::ProcessSnapshotRecoveryRequest(SnapshotRecoveryRequest* req,std::string& save_error){
	LOG(INFO) << "recover.request.reset.peers";
	this->state_machine_->Recovery(req->state);
	{
		abb::Mutex::Locker l(mtx_);
		this->peermap_.clear();
		for(int i=0;i<req->Peers.size();i++){
			LOG(DEBUG) << "recover.request." << req->Peers[i].name << " "<< req->Peers[i].addr;
			this->AddPeer(req->Peers[i].name,req->Peers[i].addr);
		}
	}
	LOG(DEBUG) << "snapshot.recover.request.LastIndex=" << req->LastIndex << ".lastTerm="<<req->LastTerm << ".LeaderName=" << req->LeaderName;
	this->cur_term_ = req->LastTerm;
	this->log_->UpdateCommitIndex(req->LastIndex);
	pending_snapshot_ = new Snapshot(req->LastIndex,req->LastTerm,req->Peers,this->GetSnapshotPath(req->LastIndex,req->LastTerm));
	pending_snapshot_->state.Write(req->state.Data(),req->state.Size());
	this->SaveSnapshot();
	this->log_->Compact(req->LastIndex,req->LastTerm);
	SnapshotRecoveryResponce * rsp = new SnapshotRecoveryResponce(req->LastTerm,true,req->LastIndex);
	return rsp;
}
bool RaftServer::SaveSnapshot(){
	if(!this->pending_snapshot_){
		return false;
	}
	if(!this->pending_snapshot_->Save()){
		return false;
	}
	Snapshot* tmp = this->snapshot_;
	this->snapshot_ = this->pending_snapshot_;
	if(tmp && !(tmp->LastIndex == snapshot_->LastIndex && tmp->LastTerm == snapshot_->LastTerm)){
		tmp->Remove();
	}
	pending_snapshot_ = NULL;
	if(tmp)delete tmp;
	return true;
}
void RaftServer::ProcessSnapshotRecoveryResponce(SnapshotRecoveryResponce* rsp){
	return;
}
void RaftServer::ProcessAppendEntriesResponce(AppendEntriesResponce* rsp){

	if(rsp->Term > this->cur_term_){
		this->SetCurrentTerm(rsp->Term,"");
		return;
	}
	if (!rsp->Success) {
		return;
	}
	if(rsp->append){
		this->sync_map_[rsp->peer] = true;
		LOG(TRACE) << "ProcessAppendEntriesResponce"  << this->sync_map_.size() << "  " << this->QuorumSize();
	}
	if(this->sync_map_.size() < this->QuorumSize()){
		return;
	}
	std::vector<uint64_t> arr;
	arr.push_back(this->log_->GetCurrentIndex());
	for(PeerMap::iterator iter = peermap_.begin();iter != peermap_.end();iter++){
		arr.push_back(iter->second->GetPreLogIndex());
	}
	std::less<uint64_t> les;
	std::sort(arr.begin(),arr.end(),les);

	uint64_t commitIndex = arr[QuorumSize()-1];
	uint64_t committedIndex = this->log_->GetCommitIndex();

	if (commitIndex > committedIndex) {
		LOG(TRACE) << "ProcessAppendEntriesResponce commitIndex:"  << commitIndex << "  committedIndex:" << committedIndex;
		this->log_->CommitToIndex(commitIndex);
		LOG(DEBUG) <<"commit index " << commitIndex;
	}
}
} /* namespace raft */
