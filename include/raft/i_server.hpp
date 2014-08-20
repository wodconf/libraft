#ifndef __RAFT_I_SERVER_HPP__
#define __RAFT_I_SERVER_HPP__
#include <string>
#include <vector>
#include <stdint.h>
namespace raft{
	class IStateMachine;
	class IMessage;
	class Commond;
	struct Config{
	public:
		Config():snapshot_check_interval(1000),snapshot_num(500),election_timeout(150),heartbeat_timeout(50){};
		~Config(){}
		uint32_t GetSnapShotCheckInterval()const{return snapshot_check_interval;}
		uint32_t GetSnapShotNum()const{return snapshot_num;}
		uint32_t GetElectionTimeout()const{return election_timeout;}
		uint32_t GetHeartBeatTimeout()const{return heartbeat_timeout;}
		void SetSnapShotCheckInterval(uint32_t value){snapshot_check_interval =value;}
		void SetSnapShotNum(uint32_t value){snapshot_num =value;}
		void SetElectionTimeout(uint32_t value){election_timeout =value;}
		void SetHeartBeatTimeout(uint32_t value){heartbeat_timeout =value;}
	private:
		uint32_t snapshot_check_interval ;
		uint64_t snapshot_num;
		uint32_t election_timeout;
		uint32_t heartbeat_timeout;
	};
	class IServer{
	public:
		enum STATE{
			LEADER,
			FOLLOWER,
			CANDIDATE,
			SNAPSHOT,
			INITED,
			STOPED
		};
		virtual ~IServer(){}
		virtual bool Init()=0;
		virtual bool Start(const std::vector<std::string>& addrlist)=0;
		virtual void Stop()=0;
		virtual void* Context()=0;
		virtual bool ApplyCommond(Commond* cmd,IMessage& rsp,std::string& save_error) = 0;
		virtual void Release()=0;
		virtual STATE State() = 0;
		virtual const std::string& Name()const=0;
		virtual const std::string& Leader()const=0;
		virtual std::string GetStateString()=0;
	};
	class IFactory{
	public:
		virtual ~IFactory(){};
		virtual Commond* GetJoinCmd() = 0;
	};
	extern IServer* NewServer(const std::string& name,
								const std::string& path,
								const std::string& addr,
								void* ctx,
								IStateMachine* machine,
								const Config& conf,IFactory* fac);
}
#endif
