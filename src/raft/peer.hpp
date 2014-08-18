
#ifndef ADCLOUD_RAFT_PEER_H_
#define ADCLOUD_RAFT_PEER_H_

#include <abb/base/thread.hpp>
#include "vote.hpp"
#include "append_entries.hpp"
#include "snapshot.hpp"
namespace raft {
class Server;
class Peer :public abb::RefObject{
public:
	Peer(Server* svr,const std::string& name,const std::string& addr);
	~Peer();
	void SetPreLogIndex(uint64_t index){
		this->pre_log_index_ = index;
	}
	uint64_t GetPreLogIndex(){
			return this->pre_log_index_ ;
		}
	void StartHeartbead();
	void StopHeartbead(bool bflush);
	
	const std::string& GetName()const{
		return name_;
	}
	const std::string& GetAddr()const{
		return addr_;
	}
	VoteResponce* SendVoteRequest(VoteRequest&req);
private:
	void WaitStop();
	static void* ThreadMain(void* arg){
		Peer* p = static_cast<Peer*>(arg);
		p->Ref();
		p->Loop();
		p->UnRef();
		return NULL;
	}
	void Loop();
	void Flush();
	void SendAppendEntriesRequest(AppendEntriesRequest&req);
	void SendSnapshotRecoveryRequest();
	void SendSnapshotRequest(SnapshotRequest&req);
	Server* svr_;
	abb::Thread thread_;
	abb::Mutex mtx_;
	std::string name_;
	std::string addr_;
	uint64_t pre_log_index_;
	bool bflush_;
	abb::Notification notify_;
	uint64_t pre_times;
	bool bstop_;
};

} /* namespace raft */

#endif /* PEER_H_ */
