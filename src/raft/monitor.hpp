#ifndef __RAFT_MONITOR_HPP__
#define __RAFT_MONITOR_HPP__

#include <abb/base/thread.hpp>
#include "raft/i_server.hpp"
namespace raft{
class Server;
class Monitor{
public:
	Monitor(Server* svr);
	~Monitor();
	void Start();
	void Stop();
private:
	void MonitorSnapshot();
	void Loop();
	static void* ThreadMonitor(void* arg){
		Monitor* p = static_cast<Monitor*>(arg);
		p->Loop();
		return NULL;
	}
	abb::Thread thread_monitor_;
	Server* svr_;
	bool bstop_;
	uint64_t last_commit_index_;
	abb::Notification notify_;
};
}

#endif
