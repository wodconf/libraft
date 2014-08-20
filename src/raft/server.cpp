

#include "raft/server.hpp"
#include "raft/messages.hpp"
#include <abb/base/log.hpp>
#include <abb/http/http_const.hpp>
#include <abb/http/http_request.hpp>
#include <abb/http/http_responce.hpp>
#include <abb/http/http_responce_writer.hpp>
#include "http_translate.hpp"
#include "event.hpp"
#include "raft/commonds.hpp"
#include "monitor.hpp"
#include "peer.hpp"
#include "log.hpp"
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
static abb::Mutex g_mtx;
static bool g_init = false;
static void Init(){
	abb::Mutex::Locker l(g_mtx);
	if(!g_init){
		g_init = true;
		RegisterCommand(JoinCommond::CMD_NAME,T_CommondCreator<JoinCommond>);
	}
	
}

extern IServer* NewServer(const std::string& name,
							const std::string& path,
							const std::string& addr,
							void* ctx,
							IStateMachine* machine,const Config& cfg,IFactory* fac){
	Init();
	return new Server(name,path,new HttpTranslate(),addr,ctx,machine,cfg,fac);
}


class VoteRequestRun:public abb::CallBack{
public:
	VoteRequestRun(Server* svr,VoteRequest* req,Peer* peer)
	:peer(peer),req(req),svr(svr){
		req->Ref();
		peer->Ref();
	}
	virtual ~VoteRequestRun(){
		req->UnRef();
		peer->UnRef();
	}
	virtual void Call(){
		VoteResponce* rsp = peer->SendVoteRequest(*req);
		if(rsp){
			rsp->snd_term = req->Term;
			svr->VoidProcessMessage(rsp);
			rsp->UnRef();
		}
		delete this;
	}
	Peer* peer;
	VoteRequest* req;
	Server* svr;
};

class StopRequest:public IMessage{
public:
	static const char*const TYPE_NAME ;
public:
	StopRequest(){};
	virtual  ~StopRequest(){}
	virtual const char* TypeName(){return TYPE_NAME;}
};
const char*const StopRequest::TYPE_NAME = "StopRequest" ;

class TakeSnapshotRequest:public IMessage{
public:
	static const char*const TYPE_NAME ;
public:
	TakeSnapshotRequest(){};
	virtual  ~TakeSnapshotRequest(){}
	virtual const char* TypeName(){return TYPE_NAME;}
};
const char*const TakeSnapshotRequest::TYPE_NAME = "TakeSnapshotRequest" ;


Server::Server(const std::string& name,
		const std::string& path,
		ITranslate* t,
		const std::string& addr,
		void* ctx,
		IStateMachine* machine,const Config& cfg,IFactory* fac)
:path_(path),
addr_(addr),
name_(name),
config_(cfg),
cur_term_(0),
state_(STOPED),
sa_chan_(10240),
itranslate_(t),
context_(ctx),
pending_snapshot_(NULL),
snapshot_(NULL),
state_machine_(machine),
fac_(fac)
{
	
	this->log_ = new LogManager(this);
	monitor_ = new Monitor(this);
	srand(time(0));
}

Server::~Server() {
	this->log_->Close();
	delete this->log_;
	delete monitor_;
}

Snapshot* Server::GetSnapshot(){
	abb::Mutex::Locker l(mtx_);
	if(snapshot_){
		snapshot_->Ref();
	}
	return snapshot_;
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
bool Server::LoadSnapshot(){
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
			LOG(WARN) << "load.snapshot  state.machine.Recovery.fail "; 
			return false;
		}
		for(unsigned i=0;i<na->Peers.size();i++){
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
bool Server::TakeSnapshot(std::string& save_error){

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
std::string Server::GetSnapshotPath(uint64_t index,uint64_t term){
	std::ostringstream s;
	s << this->path_ << "/snapshot/" << term << "_" << index << ".ss";
	return s.str();
}
IMessage* Server::ProcessMessage(IMessage* req,std::string& save_error){
	if(!req){
		save_error = "ARGUMENT_ERROR";
		return NULL;
	}
	NotifyEvent* ev = new NotifyEvent(req);
	ev->Ref();
	this->sa_chan_.Push(ev);
	ev->Wait();
	save_error = ev->err;
	IMessage* rsp = ev->rsp;
	ev->UnRef();
	return rsp;
}
void Server::AsyncProcessMessage(IMessage* msg,EventCallback* cb,void* arg){
	if(!msg){
		return;
	}
	CallBackEvent* ev = new CallBackEvent(msg,cb,arg);
	this->sa_chan_.Push(ev);
}
void Server::VoidProcessMessage(IMessage*msg){
	if(!msg){
		return;
	}
	EventBase* ev = new EventBase(msg);
	this->sa_chan_.Push(ev);
}
bool Server::FlushCommitIndex(){
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
bool Server::ReadCommitIndex(){
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
	if(ret < (int)sizeof(index)){
		return true;
	}
	this->log_->UpdateCommitIndex(index);
	return true;
}
bool Server::ApplyCommond(Commond* cmd,IMessage& rsp,std::string& save_error){
	if(this->State() == LEADER){
		IMessage* ret = this->ProcessMessage(cmd,save_error);
		if(ret){
			abb::Buffer buf;
			ret->Encode(buf);
			rsp.Decode(buf);
			return true;
		}else if(!save_error.empty()){
			return false;
		}else{
			save_error = "cmd ret is null";
			return false;
		}
	}else{
		std::string addr;
		{
			abb::Mutex::Locker l(mtx_);
			if(leader_addr_.empty()){
				save_error = "no.leader";
				return false;
			}
			addr = leader_addr_;
		}
		return this->itranslate_->SendCommond(this,addr,*cmd,rsp,save_error);
	}
	
}
void Server::OnAppendEntriesResponce(AppendEntriesResponce*msg){VoidProcessMessage(msg);}
void Server::OnSnapshotRecoveryResponce(SnapshotRecoveryResponce* msg){VoidProcessMessage(msg);}
bool Server::AddPeer( const std::string&name, const std::string& connectiongString){
	LOG(INFO) << "raft.addr.peer.name = " << name << " .addr = "<< connectiongString << " .state = " << GetStateString();
	PeerMap::iterator iter = this->peermap_.find(name);
	if(iter != this->peermap_.end()){
		FlushCommitIndex();
		return true;
	}
	if(this->name_ != name){
		Peer* peer = new Peer(this,name,connectiongString);
		this->peermap_[name] = peer;
		if(this->state_ == LEADER){
			peer->StartHeartbead();
		}
		if(this->leader_ == name){
			abb::Mutex::Locker l(mtx_);
			leader_addr_ = connectiongString;
		}
	}
	FlushCommitIndex();
	return true;
}
bool Server::RemovePeer( const std::string&name){
	LOG(INFO) << "raft.remove.peer.name = " << name << " .state = " << GetStateString();
	Peer* peer = NULL;
	bool needstop_ = false;
	{
		if(this->name_ != name){
			PeerMap::iterator iter = this->peermap_.find(name);
			if(iter == this->peermap_.end()){
				FlushCommitIndex();
				return false;
			}
			// Stop peer and remove it.
			needstop_ = (this->state_ == LEADER);
			peer = iter->second;
			this->peermap_.erase(iter);
		}
		FlushCommitIndex();
	}
	if(peer){
		if(needstop_){
			peer->StopHeartbead(true);
		}
		peer->UnRef();
	}
	return true;
}
bool Server::Init(){
	if(Running()){
		LOG(DEBUG) << "Raft Is Running" << this->state_;
		return false;
	}
	if(this->State() == INITED){
		return true;
	}
	this->LoadSnapshot();
	abb::net::IPAddr addr;
	if(! addr.SetByStringIgnoreIP(addr_) ){
		return false;
	}
	int error;
	http_svr_.Init(4,false);
	http_svr_.SetListener(this);
	if( !http_svr_.Bind(addr,&error)){
		LOG(WARN) << "bind addr fail:" << addr_ << errno<< "err:"<< strerror(errno);
		return false;
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
bool Server::Start(const std::vector<std::string>& addrlist){
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
	this->http_svr_.Start();

	bool success = false;

	if(!IsLogEmpty()){
		success = true;
	}else{
		JoinCommond* cmd = new JoinCommond(this->name_,this->addr_,this->fac_->GetJoinCmd());
		std::string err;
		if(addrlist.size() == 0){
			BoolMessage* rsp = static_cast<BoolMessage*>(this->ProcessMessage(cmd,err));
			if(rsp){
				success = rsp->success;
				rsp->UnRef();
			}else{
				LOG(WARN) << "raft.server.start.fail process.join.cmd.error "<<err;
			}

		}else{

			for(unsigned i=0;i<addrlist.size();i++){
				LOG(INFO) << "JoinCluster" << addrlist[i];
				BoolMessage rsp;
				if( this->itranslate_->SendCommond(this,addrlist[i],*cmd,rsp,err) ){
					success = rsp.success;
					if(success) break;
				}else{
					LOG(WARN) << "raft.server.start.fail process.join.cmd.error "<<err;
				}
			}
		}
	}
	if(!success){
		LOG(INFO) << "Start.Fail";
		this->Stop();
	}else{
		LOG(INFO) << "Start.Success";
		monitor_->Start();
	}
	return success;
}
static void HttpEventCallBack(void* arg,const EventBase* event_base,IMessage* msg,const std::string& err){
	abb::http::ResponceWriter* rspw = static_cast<abb::http::ResponceWriter*>(arg); 
	if(msg){
		rspw->GetResponce().SetStatusCode(abb::http::code::StatusOK);
		rspw->GetResponce().GetHeader().Set(abb::http::header::CONTENT_TYPE,"application/octet-stream");
		msg->Encode( rspw->GetResponce().Body() );
		msg->UnRef();
	}else{
		rspw->GetResponce().SetStatusCode(abb::http::code::StatusInternalServerError);
		rspw->GetResponce().GetHeader().Set(abb::http::header::CONTENT_TYPE,"text/plain");
		rspw->GetResponce().Body().Write(err.c_str(),err.size());
	}
	rspw->Flush();
	rspw->UnRef();
} 
void Server::HandleRequest(abb::http::Request* req,abb::http::ResponceWriter* rspw){
	if(req->Method() == abb::http::method::POST ){
		IMessage* reqmsg = NULL;
		std::string path = req->GetURL().Path;
		if( !path.empty()){
			path = path.substr(1);
		}
		if(path == AppendEntriesRequest::TYPE_NAME){
			reqmsg = new AppendEntriesRequest();
		}else if(path == VoteRequest::TYPE_NAME){
			reqmsg = new VoteRequest();
		}else if(path == SnapshotRequest::TYPE_NAME){
			reqmsg = new SnapshotRequest();
		}else if(path == SnapshotRecoveryRequest::TYPE_NAME){
			reqmsg = new SnapshotRecoveryRequest();
		}else{
			int pos = path.find('/');
			if(pos == -1){
				rspw->GetResponce().SetStatusCode(abb::http::code::StatusMethodNotAllowed);
				LOG(WARN) << "unknow path " << path;
			}else{
				std::string type = path.substr(0,pos);
				std::string  cmd = path.substr(pos+1);
				if(type == Commond::TYPE_NAME){
					reqmsg = NewCommond(cmd);
				}else{
					LOG(WARN) << "unknow path " << path;
					rspw->GetResponce().SetStatusCode(abb::http::code::StatusMethodNotAllowed);
				}
			}
		}
		if(reqmsg){
			if(!reqmsg->Decode(req->Body())){
				LOG(INFO) << "decode fail" << path;
				std::string error = "decode fail";
				rspw->GetResponce().SetStatusCode(abb::http::code::StatusInternalServerError);
				rspw->GetResponce().GetHeader().Set(abb::http::header::CONTENT_TYPE,"text/plain");
				rspw->GetResponce().Body().Write(error.c_str(),error.size());
			}else{
				rspw->Ref();
				AsyncProcessMessage(reqmsg,HttpEventCallBack,rspw);
				reqmsg->UnRef();
				return;
			}
		}
		rspw->Flush();
	}
}
std::string Server::GetStateString(){
	abb::Mutex::Locker l(mtx_);
	static const char* STATE_NAME[] = {"LEADER", "FOLLOWER", "CANDIDATE", "STOPED","INITED"};
	std::ostringstream s;
	s << "Name: " << this->name_  
	<< ", State: " << STATE_NAME[state_] 
	<< ", Term: " << this->cur_term_ 
	<< ", CommitedIndex: " << this->CommitIndex()
	<< ", LeaderAddr: " << this->leader_addr_;
	return s.str();
}
void Server::SetState(STATE s) {
	this->state_ = s;
	if (this->state_ == LEADER){
		this->SetLeaderName(this->name_);
		this->sync_map_.clear();
	}
}
void Server::Release(){
	Stop();
	delete this;
}
void Server::Stop(){
	if(!Running()){
		LOG(DEBUG) << "Raft Is NOt Running" << this->state_;
		return ;
	}
	StopRequest* req = new StopRequest() ;
	std::string err;
	this->ProcessMessage(req,err);
	this->http_svr_.Close();
}

bool Server::ProcessIfStopEvent(EventBase* ev){
	if(ev->Request()->TypeName() == StopRequest::TYPE_NAME){
		this->SetState(STOPED);
		ev->Notify(NULL,"");
		return true;
	}
	return false;
}
bool Server::StartTakeSnapshot(std::string& save_error){
	BoolMessage* b = static_cast<BoolMessage*>( this->ProcessMessage(new TakeSnapshotRequest(),save_error) );
	if(b){
		bool suc = b->success;
		b->UnRef();
		return suc;
	}
	return false;
}
bool Server::ProcessIfTakeSnapshot(EventBase*ev){
	if(ev->Request()->TypeName() == TakeSnapshotRequest::TYPE_NAME){
		std::string error;
		bool suc = this->TakeSnapshot(error);
		ev->Notify(new BoolMessage(suc),error);
		return true;
	}
	return false;
}
bool Server::ProcessIfSnapshotRequest(EventBase* ev){
	if( ev->Request()->TypeName() == SnapshotRequest::TYPE_NAME ){
		SnapshotRequest* req = static_cast<SnapshotRequest*>(ev->Request());
		LogEntry* entry = this->log_->GetLogEntry(req->LastIndex);
		IMessage* back = NULL;
		if(entry && entry->term_ == req->LastTerm){
			back = new SnapshotResponce(false);
		}else{
			this->SetState(SNAPSHOT);
			back = new SnapshotResponce(true);
		}
		ev->Notify(back,"");
		return true;
	}
	return false;
}
bool Server::ProcessIfAppendEntriesRequest(EventBase* ev,bool* bupdate){
	if( ev->Request()->TypeName() == AppendEntriesRequest::TYPE_NAME ){
		std::string err;
		IMessage* back = this->ProcessAppendEntriesRequest(static_cast<AppendEntriesRequest*>(ev->Request()),err,bupdate);
		ev->Notify(back,err);
		return true;
	}
	return false;
}
bool Server::ProcessIfAppendEntriesResponce(EventBase* ev){
	if(ev->Request()->TypeName() == AppendEntriesResponce::TYPE_NAME){
		this->ProcessAppendEntriesResponce(static_cast<AppendEntriesResponce*>(ev->Request()));
		ev->Notify(NULL,"");
		return true;
	}
	return false;
}
bool Server::ProcessIfSnapshotRecoveryRequest(EventBase* ev){
	if(ev->Request()->TypeName() == SnapshotRecoveryRequest::TYPE_NAME){
		std::string err;
		IMessage* back= this->ProcessSnapshotRecoveryRequest(static_cast<SnapshotRecoveryRequest*>(ev->Request()),err);
		ev->Notify(back,err);
		return true;
	}
	return false;
}
bool Server::ProcessIfSnapshotRecoveryResponce(EventBase* ev){
	if(ev->Request()->TypeName() == SnapshotRecoveryResponce::TYPE_NAME){
		this->ProcessSnapshotRecoveryResponce(static_cast<SnapshotRecoveryResponce*>(ev->Request()));
		ev->Notify(NULL,"");
		return true;
	}
	return false;
}
bool Server::ProcessIfVoteRequest(EventBase* ev,bool* bupdate){
	if(ev->Request()->TypeName() == VoteRequest::TYPE_NAME){
		std::string err;
		IMessage* back= this->ProcessVoteRequest(static_cast<VoteRequest*>(ev->Request()),err,bupdate);
		ev->Notify(back,err);
		return true;
	}
	return false;
}
bool Server::ProcessIfJoinCmd(EventBase* ev){
	if(ev->Request()->TypeName() == Commond::TYPE_NAME){
		Commond* cmd = static_cast<Commond*>(ev->Request());
		if(cmd->CommondName() == JoinCommond::CMD_NAME){
			JoinCommond* cmd = static_cast<JoinCommond*>( ev->Request());
			if(this->log_->GetCurrentIndex() == 0 && cmd->NodeName() == this->name_){
				LOG(DEBUG) << "selfjoin and promote to leader";
				this->SetState(LEADER);
				this->ProcessCommond(static_cast<Commond*>(ev->Request()),ev);
			}else{
				ev->Notify(NULL,"not.leader.error");
			}
			return true;
		}
	}
	return false;
}
bool Server::ProcessIfVoteResponce(EventBase* ev,unsigned *vote){
	if(ev->Request()->TypeName() == VoteResponce::TYPE_NAME){
		VoteResponce* rsp = static_cast<VoteResponce*>(ev->Request());
		if(this->cur_term_ == rsp->snd_term){
			if(rsp->VoteGranted){
				LOG(DEBUG)<< ("server.candidate.vote.success");
				if(vote)(*vote)++;
			}else if (rsp->Term > this->cur_term_) {
				LOG(DEBUG)<< ("server.candidate.vote.failed");
				SetCurrentTerm(rsp->Term, "");
			}else{
				LOG(DEBUG)<< ("server.candidate.vote:denied");
			}
		}
		ev->Notify(NULL,"");
		return true;
	}
	return false;
}

bool Server::ProcessIfCmd(EventBase*ev){
	if(ev->Request()->TypeName() == Commond::TYPE_NAME){
		this->ProcessCommond(static_cast<Commond*>(ev->Request()),ev);
		return true;
	}
	return false;
}
void Server::Loop(){
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
}void Server::SnapshotLoop(){
	LOG(INFO) << "I am SNAPSHOT";
	this->SetState(SNAPSHOT);
	while(this->State() == SNAPSHOT){
		EventBase* ev = this->sa_chan_.Poll();
		if(!ev || !ev->Request()){
			continue;
		}
		ev->SetStart();
		LOG(TRACE) << "SnapshotLoop process event:" << ev->Request()->TypeName();
		if(!ProcessIfStopEvent(ev)){
			if(!ProcessIfTakeSnapshot(ev))
				if(!ProcessIfAppendEntriesRequest(ev,NULL)){
					if(!ProcessIfSnapshotRecoveryRequest(ev)){
						if(!ProcessIfVoteRequest(ev,NULL)){
							LOG(WARN) << "snapshot.not.deal.this.event" << ev->Request()->TypeName();
							ev->Notify(NULL,"not.leader.error");
						}
					}
				}
		}
		ev->UnRef();
	}
}
void Server::LeaderLoop(){
	LOG(INFO) << "I am Leader";
	this->SetState(LEADER);
	uint64_t log_index;
	this->log_->GetLastInfo(&log_index,NULL);
	for(PeerMap::iterator iter = peermap_.begin();iter != peermap_.end();iter++){
		iter->second->SetPreLogIndex(log_index);
		iter->second->StartHeartbead();
	}
	while(this->State() == LEADER){
		EventBase* ev = this->sa_chan_.Poll();
		if(!ev || !ev->Request()){
			continue;
		}
		ev->SetStart();
		LOG(TRACE) << "LeaderLoop process event:" << ev->Request()->TypeName();
		if(!ProcessIfStopEvent(ev)){
			if(!ProcessIfTakeSnapshot(ev))
				if(!ProcessIfAppendEntriesRequest(ev,NULL)){
					if(!ProcessIfAppendEntriesResponce(ev)){
						if(!ProcessIfVoteRequest(ev,NULL)){
							if(!ProcessIfCmd(ev)){
								LOG(WARN) << "leader.not.deal.this.event" << ev->Request()->TypeName();
								ev->Notify(NULL,"not.leader.error");
							}
						}
					}
				}
		}
		ev->UnRef();
	}
	this->sync_map_.clear();
}
void Server::FollowerLoop(){
	LOG(INFO) << "I am Follower";
	this->SetState(FOLLOWER);
	int e_timeout = this->config_.GetElectionTimeout();
	int timeout = e_timeout + rand()%e_timeout;
	while(this->State() == FOLLOWER){
		EventBase* ev = NULL;
		bool bupdate = false;
		uint64_t now = abb::Date::Now().MilliSecond();
		if( !this->sa_chan_.PollTimeout(timeout,&ev) || timeout < 0 ){
			LOG(DEBUG) << "follower.wait.timeout:"<< timeout << " time:" << (abb::Date::Now().MilliSecond()-now);
			if(this->Promotable()){
				this->SetState(CANDIDATE);
				break;
			}else{
				bupdate = true;
			}
		}else{
			if(!ev || !ev->Request()){
				LOG(DEBUG) << "EMPTY";
				continue;
			}
			ev->SetStart();
			LOG(TRACE) << "FollowerLoop process event:" << ev->Request()->TypeName();
			if(!ProcessIfStopEvent(ev)){
				if(!ProcessIfTakeSnapshot(ev)){
					if(!ProcessIfAppendEntriesRequest(ev,&bupdate)){
						if(!ProcessIfAppendEntriesResponce(ev)){
							if(!ProcessIfVoteRequest(ev,&bupdate)){
								if(!ProcessIfSnapshotRequest(ev)){
									if(!ProcessIfJoinCmd(ev)){
										LOG(WARN) << "follower.not.deal.this.event  " <<ev->Request()->TypeName();
										ev->Notify(NULL,"not.leader.error");
									}
								}
							}
						}
					}
				}else{
					bupdate = true;
				}
					
			}
			ev->UnRef();
		}

		if(bupdate){
			timeout = e_timeout + rand()%e_timeout;
		}else{
			timeout -= (abb::Date::Now().MilliSecond() - now);
		}
	}
}
void Server::CandidateLoop(){
	LOG(INFO) << "I am Candidate";
	uint64_t last_index,last_term;
	this->log_->GetLastInfo(&last_index,&last_term);
	std::string preleader = this->leader_;
	this->leader_ = "";
	bool req_vote = true;
	int timeout = 0;
	int e_timeout = this->config_.GetElectionTimeout();
	unsigned votesGranted = 0;
	while(this->State() == CANDIDATE){
		if( req_vote ){
			this->cur_term_++;
			this->voted_for_ = this->name_;
			timeout = e_timeout + rand()%e_timeout;
			LOG(INFO) << "start_vote" << this->cur_term_ << "timeout:" << timeout;
			VoteRequest* req = new VoteRequest(this->cur_term_,last_index,last_term,this->name_);
			for(PeerMap::iterator iter = peermap_.begin();iter != peermap_.end();iter++){
				this->pool.Execute(new VoteRequestRun(this,req,iter->second));
			}
			req->UnRef();
			req_vote = false;

			
			votesGranted = 1;
		}
		if (votesGranted >= this->QuorumSize()) {
			LOG(INFO) << "vote.win" << this->name_;
			this->SetState(LEADER);
			break;
		}
		EventBase* ev = NULL;
		uint64_t now = abb::Date::Now().MilliSecond();
		if( this->sa_chan_.PollTimeout(timeout,&ev) ){
			if(ev  && ev->Request()){
				ev->SetStart();
				LOG(TRACE) << "CandidateLoop process event:" << ev->Request()->TypeName();
				if(!ProcessIfStopEvent(ev)){
					if(!ProcessIfTakeSnapshot(ev))
						if(!ProcessIfAppendEntriesRequest(ev,NULL)){
							if(!ProcessIfVoteRequest(ev,NULL)){
								if(!ProcessIfVoteResponce(ev,&votesGranted)){
									LOG(WARN) << "candidate.not.deal.this.event" << ev->Request()->TypeName();
									ev->Notify(NULL, "not.leader.error");
								}
							}
						}
				}
				ev->UnRef();
			}
			timeout -= (abb::Date::Now().MilliSecond() - now);
			if(timeout <= 0){
				req_vote = true;
			}
		}else{
			req_vote = true;
		}
		
	}

}

void Server::SetCurrentTerm(uint64_t term,const std::string& leaderName){
	if(term > this->cur_term_){
		if(this->state_ == LEADER){
			for(PeerMap::iterator iter = peermap_.begin();iter != peermap_.end();iter++){
				iter->second->StopHeartbead(false);
			}
		}
		this->state_ = FOLLOWER;
		SetLeaderName(leaderName);
		this->voted_for_ = "";
		this->cur_term_ = term;
		LOG(INFO) << "[update.current.term]  " << term;
	}else{
		LOG(DEBUG) << "SetCurrentTerm Fail" << term << leaderName << "cur_term_" << cur_term_;
	}
}
void Server::SetLeaderName(const std::string & name){
	if(this->leader_ != name){
		LOG(INFO) << "[change.leader] " << this->leader_ << "->" << name;
		this->leader_ = name;
		if(this->leader_.empty()){
			LOG(INFO) << "[change.leader.addr] " << leader_addr_ << " to " << "";
			abb::Mutex::Locker l(mtx_);
			leader_addr_ = "";
		}else{
			abb::Mutex::Locker l(mtx_);
			if(this->leader_ == this->name_){
				LOG(INFO) << "[change.leader.addr] " << leader_addr_ << " to " << this->addr_;
				leader_addr_ = this->addr_;
			}else{
				PeerMap::iterator iter = this->peermap_.find(leader_);
				if( iter != this->peermap_.end()){
					LOG(INFO) << "[change.leader.addr] " << leader_addr_ << " to " << iter->second->GetAddr();
					leader_addr_ = iter->second->GetAddr();
				}else{
					LOG(INFO) << "[change.leader.addr] " << leader_addr_ << " to " << "";
					leader_addr_ = "";
				}
			}
		}
	}
}
AppendEntriesResponce* Server::ProcessAppendEntriesRequest(AppendEntriesRequest* req,std::string& save_error,bool* bupdate){
	if(req->term < this->cur_term_){
		LOG(WARN) << "process.append.entries.term.fial [" << req->term << ":" << this->cur_term_ << "]" ;
		if(bupdate)*bupdate = false;
		return new AppendEntriesResponce(cur_term_,this->log_->GetCurrentIndex(),false,this->log_->GetCommitIndex());
	}
	if(req->term == this->cur_term_){
		this->state_ = FOLLOWER;
		SetLeaderName(req->LeaderName);
	}else{
		SetCurrentTerm(req->term,req->LeaderName);
	}
	if(bupdate)*bupdate = true;
	if( !this->log_->Truncate(req->PrevLogIndex,req->PrevLogTerm) ){
		return new AppendEntriesResponce(cur_term_,this->log_->GetCurrentIndex(),false,this->log_->GetCommitIndex());
	}
	if(! this->log_->AppendEntries(req->Entries,NULL)){
		return new AppendEntriesResponce(cur_term_,this->log_->GetCurrentIndex(),false,this->log_->GetCommitIndex());
	}
	if(!this->log_->CommitToIndex(req->CommitIndex)){
		return new AppendEntriesResponce(cur_term_,this->log_->GetCurrentIndex(),false,this->log_->GetCommitIndex());
	}
	return new AppendEntriesResponce(cur_term_,this->log_->GetCurrentIndex(),true,this->log_->GetCommitIndex());
}
VoteResponce* Server::ProcessVoteRequest(VoteRequest* req,std::string& save_error,bool* bupdate){

	if(req->Term < this->cur_term_){
		LOG(DEBUG) << "vote.fail  term.small.vote.false " << req->CandidateName << " value"<< req->Term << ":" << this->cur_term_;
		if(bupdate)*bupdate = false;
		return new VoteResponce(cur_term_,false);
	}
	if(req->Term > this->cur_term_){
		SetCurrentTerm(req->Term,"");
	}else if(this->voted_for_ != "" && this->voted_for_ != req->CandidateName){
		LOG(DEBUG) << "vote.fail  has.vote.to ( " << voted_for_ << " ).req.name = " << req->CandidateName;
		if(bupdate)*bupdate = false;
		return new VoteResponce(cur_term_,false);
	}
	uint64_t last_index,last_term;
	this->log_->GetLastInfo(&last_index,&last_term);

	if(last_index > req->LastLogIndex || last_term > req->LastLogTerm){
		LOG(DEBUG) << "vote.fail  term.small.or.index.small.vote.fail " <<  req->CandidateName 
		<< " index " << last_index << ":" << req->LastLogIndex
		<< " term " << last_term << ":" << req->LastLogTerm;
		if(bupdate)*bupdate = false;
		return new VoteResponce(cur_term_,false);
	}
	LOG(DEBUG) << "vote.success  vote.to.req.name = " << req->CandidateName 
	<< " index " << last_index << ":" << req->LastLogIndex
	<< " term " << last_term << ":" << req->LastLogTerm;
	this->voted_for_ = req->CandidateName;
	if(bupdate)*bupdate = true;
	return new VoteResponce(cur_term_,true);
}
void Server::ProcessCommond(Commond* req,EventBase* ev){
	LOG(DEBUG) << "server.command.process = " << req->CommondName();
	LogEntry* ent = log_->CreateEntry(this->cur_term_,req,ev);
	if(!ent){
		LOG(DEBUG) << "server.command.process.create.entry.fail ";
		ev->Notify(NULL,"server.command.log.entry.error:creatfail");
		return;
	}
	std::string err;
	if(!this->log_->AppendEntry(ent,&err)){
		LOG(DEBUG) << "server.command.process.append.entry.fail " << err;
		ev->Notify(NULL,err);
		ent->UnRef();
		return;
	}
	this->sync_map_[this->name_] = true;
	if(this->peermap_.size() == 0){
		uint64_t cindex = this->log_->GetCurrentIndex();
		this->log_->CommitToIndex(cindex);
		LOG(DEBUG) <<"commit index " << cindex;
	}
}
SnapshotRecoveryResponce* Server::ProcessSnapshotRecoveryRequest(SnapshotRecoveryRequest* req,std::string& save_error){
	LOG(INFO) << "recover.request.reset.peers";
	this->state_machine_->Recovery(req->state);
	this->peermap_.clear();
	for(unsigned i=0;i<req->Peers.size();i++){
		LOG(DEBUG) << "recover.request." << req->Peers[i].name << " "<< req->Peers[i].addr;
		this->AddPeer(req->Peers[i].name,req->Peers[i].addr);
	}
	LOG(DEBUG) << "snapshot.recover.request.LastIndex=" << req->LastIndex << ".lastTerm="<<req->LastTerm << ".LeaderName=" << req->LeaderName;
	this->cur_term_ = req->LastTerm;
	this->log_->UpdateCommitIndex(req->LastIndex);
	pending_snapshot_ = new Snapshot(req->LastIndex,req->LastTerm,req->Peers,this->GetSnapshotPath(req->LastIndex,req->LastTerm));
	pending_snapshot_->state.Write(req->state.ReadPtr(),req->state.ReadSize());
	this->SaveSnapshot();
	this->log_->Compact(req->LastIndex,req->LastTerm);
	SnapshotRecoveryResponce * rsp = new SnapshotRecoveryResponce(req->LastTerm,true,req->LastIndex);
	return rsp;
}
bool Server::SaveSnapshot(){
	if(!this->pending_snapshot_){
		return false;
	}
	if(!this->pending_snapshot_->Save()){
		return false;
	}
	Snapshot* tmp = NULL;
	{
		abb::Mutex::Locker l(mtx_);
		tmp = this->snapshot_;
		this->snapshot_ = this->pending_snapshot_;
	}
	
	if(tmp && !(tmp->LastIndex == snapshot_->LastIndex && tmp->LastTerm == snapshot_->LastTerm)){
		tmp->Remove();
	}
	pending_snapshot_ = NULL;
	if(tmp)tmp->UnRef();
	return true;
}
void Server::ProcessSnapshotRecoveryResponce(SnapshotRecoveryResponce* rsp){
	return;
}
void Server::ProcessAppendEntriesResponce(AppendEntriesResponce* rsp){

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
	std::greater<uint64_t> gtr;
	std::sort(arr.begin(),arr.end(),gtr);

	uint64_t commitIndex = arr[QuorumSize()-1];
	uint64_t committedIndex = this->log_->GetCommitIndex();

	if (commitIndex > committedIndex) {
		LOG(TRACE) << "ProcessAppendEntriesResponce commitIndex:"  << commitIndex << "  committedIndex:" << committedIndex;
		this->log_->CommitToIndex(commitIndex);
		LOG(DEBUG) <<"commit index " << commitIndex;
	}
}
} /* namespace raft */
