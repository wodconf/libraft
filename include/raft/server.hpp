
#ifndef __RAFT_SERVER_HPP__
#define __RAFT_SERVER_HPP__

#include <stdint.h>
#include <map>

#include <abb/base/thread.hpp>
#include <abb/base/block_queue.hpp>
#include <abb/base/thread_pool.hpp>
#include "commond.hpp"
#include "i_translate.hpp"
#include "i_state_machine.hpp"
namespace raft {
struct Event;
class LogManager;
class Peer;
class AppendEntriesRequest;
class AppendEntriesResponce;
class Snapshot;
class SnapshotRecoveryResponce;
class VoteResponce;
class VoteRequest;
class SnapshotRecoveryRequest;
class Server {
public:
	enum STATE{
		LEADER,
		FOLLOWER,
		CANDIDATE,
		SNAPSHOT,
		INITED,
		STOPED
	};
public:
	Server(const std::string& name,
			const std::string& path,
			ITranslate* t,
			const std::string& connectiongString,
			void* ctx,
			IStateMachine* machine);
	~Server();
	IMessage* ProcessMessage(IMessage* req,std::string& save_error);
	void* DealCommond(Commond* cmd,std::string& save_error);

	bool LoadSnapshot();
	bool TakeSnapshot(std::string& save_error);
	void OnAppendEntriesResponce(AppendEntriesResponce*);
	void OnSnapshotRecoveryResponce(SnapshotRecoveryResponce* rsp);
	bool AddPeer( const std::string&name, const std::string& connectiongString);
	bool RemovePeer( const std::string&name);
	bool Init();
	bool Start();
	void Stop();

	const std::string& Name() const{return name_;}
	const std::string& Leader()const{return leader_;}
	STATE State(){
		abb::Mutex::Locker l(mtx_);
		return state_;
	}
	void* Context()const{return context_;}
	uint64_t Term(){
		abb::Mutex::Locker l(mtx_);
		return cur_term_;
	}
	uint64_t CommitIndex();
	const std::string& VotedFor(){
		return voted_for_;
	}
	uint32_t ElectionTimeout()const{return election_time_;}
	void SetElectionTimeout(uint32_t timeout){election_time_ = timeout;}
	uint32_t HeartbeatTimeout()const{return heartbeat_time_;}
	void SetHeartbeatTimeout(uint32_t timeout){heartbeat_time_ = timeout;}
	ITranslate* Transporter()const{return itranslate_;};
	typedef std::map<std::string,Peer*> PeerMap;
	PeerMap Peers()const{return peermap_;}
	bool Running(){
		abb::Mutex::Locker l(mtx_);
		return ( this->state_ != STOPED && this->state_ != INITED);
	}
	bool GetLeaderAddr(std::string& addr,std::string* error);
	//
	LogManager* GetLog(){
		return log_;
	}
	const std::string GetAddr(){
		return addr_;
	}
	int QuorumSize();
	Snapshot* GetSnapshot(){
		return snapshot_;
	}
	bool IsLogEmpty();
	const char* GetStateString(){
		abb::Mutex::Locker l(mtx_);
		if(state_ == LEADER){
			return "LEADER";
		}else if(state_ == FOLLOWER){
			return "FOLLOWER";
		}else if(state_ == CANDIDATE){
			return "CANDIDATE";
		}else if(state_ == SNAPSHOT){
			return "SNAPSHOT";
		}else if(state_ == STOPED){
			return "STOPED";
		}else if(state_ == INITED){
			return "INITED";
		}else{
			return "UNKNOW";
		}
	}
private:
	bool Promotable();
	void SetCurrentTerm(uint64_t term,const std::string& leaderName);
	void* send(IMessage*,std::string& save_error);
	void LeaderLoop();
	void FollowerLoop();
	void CandidateLoop();
	void SnapshotLoop();
	AppendEntriesResponce* ProcessAppendEntriesRequest(AppendEntriesRequest* req,std::string& save_error,bool* bupdate);
	VoteResponce* ProcessVoteRequest(VoteRequest* req,std::string& save_error,bool* bupdate);
	SnapshotRecoveryResponce* ProcessSnapshotRecoveryRequest(SnapshotRecoveryRequest* req,std::string& save_error);
	void ProcessCommond(Commond* req,Event* ev);
	void ProcessAppendEntriesResponce(AppendEntriesResponce* rsp);
	void ProcessSnapshotRecoveryResponce(SnapshotRecoveryResponce* rsp);

	bool ProcessIfSnapshotRecoveryRequest(Event* ev);
	bool ProcessIfSnapshotRecoveryResponce(Event* ev);
	bool ProcessIfSnapshotRequest(Event* ev);
	bool ProcessIfStopEvent(Event* ev);
	bool ProcessIfAppendEntriesRequest(Event* ev,bool* bupdate);
	bool ProcessIfAppendEntriesResponce(Event* ev);
	bool ProcessIfVoteRequest(Event* ev,bool* bupdate);
	bool ProcessIfJoinCmd(Event* ev);
	bool ProcessIfCmd(Event*ev);
	bool ProcessIfVoteResponce(Event*ev,int*vote);

	std::string GetSnapshotPath(uint64_t index,uint64_t term);
	bool SaveSnapshot();
	bool FlushCommitIndex();
	bool ReadCommitIndex();
private:
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
	LogManager* log_;
	ITranslate* itranslate_;
	std::string leader_;
	abb::Thread thread_;
	STATE state_;
	uint64_t cur_term_;
	void* context_;
	std::string voted_for_;
	PeerMap peermap_;
	uint32_t election_time_;
	uint32_t heartbeat_time_;
	abb::Mutex mtx_;
	std::string name_;
	abb::BlockQueue<Event*> sa_chan_;
	typedef std::map<std::string,bool> SyncdMap;
	SyncdMap sync_map_;
	Snapshot* snapshot_;
	Snapshot* pending_snapshot_;
	abb::ThreadPool pool;
	IStateMachine* state_machine_;
};

} /* namespace raft */

#endif /* RAFT_SERVER_HPP_ */
