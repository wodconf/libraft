/*
 * snapshot.hpp
 *
 *  Created on: 2014-5-14
 *      Author: wd
 */

#ifndef SNAPSHOT_HPP_
#define SNAPSHOT_HPP_

#include <string>
#include <vector>
#include <abb/base/buffer.hpp>
#include "raft/i_message.hpp"
namespace raft {
struct PeerInfo{
	std::string name;
	std::string addr;
};
class Snapshot {
public:
	Snapshot();
	Snapshot(uint64_t LastIndex,
			uint64_t LastTerm,
			const std::vector<PeerInfo>& peers,
			const std::string& path);
	virtual ~Snapshot();
	uint64_t LastIndex;
	uint64_t LastTerm;
	std::vector<PeerInfo> Peers;
	std::string Path;
	abb::Buffer state;
	bool Save();
	void Remove();
	void Encode(abb::Buffer &buf);
	void Decode(abb::Buffer &buf);
	static Snapshot* Load(const std::string& path);
};
class SnapshotRequest:public  IMessage{
public:
	static const char* const TYPE_NAME = "SnapshotRequest" ;
public:
	SnapshotRequest();
	SnapshotRequest(const std::string& LeaderName,const Snapshot& snap);
	~SnapshotRequest();
	uint64_t LastIndex;
	uint64_t LastTerm;
	std::string LeaderName;
	virtual const char* TypeName(){
		return TYPE_NAME;
	}
	virtual bool Encode(abb::Buffer& buf);
	virtual bool Decode(abb::Buffer& buf);
};
class SnapshotResponce:public IMessage{
public:
	static const char* const TYPE_NAME = "SnapshotResponce" ;
public:
	SnapshotResponce();
	SnapshotResponce(bool suc);
	~SnapshotResponce();
	bool success;
	virtual const char* TypeName(){
		return TYPE_NAME;
	}
	virtual bool Encode(abb::Buffer& buf);
	virtual bool Decode(abb::Buffer& buf);
};
class SnapshotRecoveryRequest :public IMessage{
public:
	static const char* const TYPE_NAME = "SnapshotRecoveryRequest" ;
public:
	SnapshotRecoveryRequest();
	SnapshotRecoveryRequest(const std::string& LeaderName,Snapshot& snap);
	~SnapshotRecoveryRequest();
	uint64_t LastIndex;
	uint64_t LastTerm;
	std::string LeaderName;
	std::vector<PeerInfo> Peers;
	abb::Buffer state;
	virtual const char* TypeName(){
		return TYPE_NAME;
	}
	virtual bool Encode(abb::Buffer& buf);
	virtual bool Decode(abb::Buffer& buf);
};

class SnapshotRecoveryResponce:public IMessage {
public:
	static const char* const TYPE_NAME = "SnapshotRecoveryResponce" ;
public:
	SnapshotRecoveryResponce();
	SnapshotRecoveryResponce(uint64_t Term,bool Success,uint64_t CommitIndex);
	~SnapshotRecoveryResponce();
	virtual const char* TypeName(){
		return TYPE_NAME;
	}
	virtual bool Encode(abb::Buffer& buf);
	virtual bool Decode(abb::Buffer& buf);
public:
	uint64_t Term;
	bool Success;
	uint64_t CommitIndex;
};
} /* namespace adcloud */

#endif /* SNAPSHOT_HPP_ */
