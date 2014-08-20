
#ifndef __RAFT_SERVER_HPP__
#define __RAFT_SERVER_HPP__


#include <abb/base/thread.hpp>
#include <abb/base/block_queue.hpp>
#include <abb/base/thread_pool.hpp>

#include <stdint.h>
#include <map>

#include "raft/commond.hpp"
#include "raft/i_translate.hpp"
#include "raft/i_state_machine.hpp"
#include "raft/i_server.hpp"
#include <abb/http/http_server.hpp>

#include "raft/event.hpp"
#include "raft/log.hpp"

namespace raft {
class LogManager;
class Peer;
class AppendEntriesRequest;
class AppendEntriesResponce;
class Snapshot;
class SnapshotRecoveryResponce;
class VoteResponce;
class VoteRequest;
class SnapshotRecoveryRequest;
class Monitor;

class Server :public IServer,public abb::http::Server::Listener{
public:
	typedef std::map<std::string,Peer*> PeerMap;
public:
	Server(const std::string& name,
			const std::string& path,
			ITranslate* t,
			const std::string& connectiongString,
			void* ctx,
			IStateMachine* machine,const Config& cfg,IFactory* fac);
	virtual ~Server();
	virtual bool ApplyCommond(Commond* cmd,IMessage& rsp,std::string& save_error);
	virtual bool Init();
	virtual bool Start(const std::vector<std::string>& addrlist);
	virtual void Stop();
	virtual void Release();
	virtual const std::string& Name() const{return name_;}
	virtual const std::string& Leader()const{return leader_;}
	virtual void* Context(){return context_;}
	virtual STATE State() {return state_;}
	virtual std::string GetStateString();

	bool AddPeer( const std::string&name, const std::string& connectiongString);
	bool RemovePeer( const std::string&name);
	bool StartTakeSnapshot(std::string& save_error);

	const Config& GetConfig() const{return config_;}
	uint64_t Term()const {return cur_term_;}
	bool Promotable(){return this->log_->GetCurrentIndex() > 0;}
	bool IsLogEmpty(){return log_->IsEmpty();}
	uint64_t CommitIndex(){return log_->GetCommitIndex();}
	const std::string& VotedFor(){return voted_for_;}
	ITranslate* Transporter()const{return itranslate_;};
	bool Running() const {return ( this->state_ != STOPED && this->state_ != INITED);}
	LogManager* GetLog(){return log_;}
	Snapshot* GetSnapshot();

	void OnAppendEntriesResponce(AppendEntriesResponce*msg);
	void OnSnapshotRecoveryResponce(SnapshotRecoveryResponce* msg);
	

private:
	virtual void HandleRequest(abb::http::Request* req,abb::http::ResponceWriter* rspw);
	unsigned QuorumSize() const{
		unsigned size = peermap_.size()+1;
		return (size/2) +1;
	}
	IMessage* ProcessMessage(IMessage*,std::string& save_error);
	void AsyncProcessMessage(IMessage*,EventCallback* cb,void* arg);
	void VoidProcessMessage(IMessage* msg);
	
	void SetCurrentTerm(uint64_t term,const std::string& leaderName);
	void LeaderLoop();
	void FollowerLoop();
	void CandidateLoop();
	void SnapshotLoop();
	AppendEntriesResponce* ProcessAppendEntriesRequest(AppendEntriesRequest* req,std::string& save_error,bool* bupdate);
	VoteResponce* ProcessVoteRequest(VoteRequest* req,std::string& save_error,bool* bupdate);
	SnapshotRecoveryResponce* ProcessSnapshotRecoveryRequest(SnapshotRecoveryRequest* req,std::string& save_error);
	void ProcessCommond(Commond* req,EventBase* ev);
	void ProcessAppendEntriesResponce(AppendEntriesResponce* rsp);
	void ProcessSnapshotRecoveryResponce(SnapshotRecoveryResponce* rsp);

	bool ProcessIfSnapshotRecoveryRequest(EventBase* ev);
	bool ProcessIfSnapshotRecoveryResponce(EventBase* ev);
	bool ProcessIfSnapshotRequest(EventBase* ev);
	bool ProcessIfStopEvent(EventBase* ev);
	bool ProcessIfAppendEntriesRequest(EventBase* ev,bool* bupdate);
	bool ProcessIfAppendEntriesResponce(EventBase* ev);
	bool ProcessIfVoteRequest(EventBase* ev,bool* bupdate);
	bool ProcessIfJoinCmd(EventBase* ev);
	bool ProcessIfCmd(EventBase*ev);
	bool ProcessIfTakeSnapshot(EventBase*ev);
	bool ProcessIfVoteResponce(EventBase*ev,unsigned*vote);

	std::string GetSnapshotPath(uint64_t index,uint64_t term);
	bool SaveSnapshot();
	bool FlushCommitIndex();
	bool ReadCommitIndex();
	bool TakeSnapshot(std::string& save_error);
	bool LoadSnapshot();
	
private:
	void SetLeaderName(const std::string & name);
	void SetState(STATE s);
	static void* ThreadMain(void* arg){
		Server* p = static_cast<Server*>(arg);
		p->Loop();
		return NULL;
	}
	
	void Loop();
	friend struct VoteRequestRun;
private:
	std::string path_;
	std::string addr_;
	std::string name_;
	std::string voted_for_;
	std::string leader_;

	Config config_;
	uint64_t cur_term_;
	STATE state_;
	abb::BlockQueue<EventBase*> sa_chan_;

	LogManager* log_;
	ITranslate* itranslate_;
	void* context_;
	Snapshot* pending_snapshot_;
	Snapshot* snapshot_;
	IStateMachine* state_machine_;
	Monitor* monitor_;
	IFactory* fac_;

	PeerMap peermap_;
	typedef std::map<std::string,bool> SyncdMap;
	SyncdMap sync_map_;
	abb::Mutex mtx_;
	abb::Thread thread_;
	abb::ThreadPool pool;
	abb::http::Server http_svr_;

	std::string leader_addr_;
	
	
};

} /* namespace raft */

#endif /* RAFT_SERVER_HPP_ */
