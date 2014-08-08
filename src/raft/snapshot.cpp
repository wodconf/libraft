
#include "snapshot.hpp"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <abb/base/log.hpp>
#include <string.h>
#include <errno.h>
#include <sys/uio.h>
namespace raft {
Snapshot::Snapshot():LastIndex(0),LastTerm(0){

}
Snapshot::Snapshot(uint64_t LastIndex,
		uint64_t LastTerm,
		const std::vector<PeerInfo>& peers,
		const std::string& path):LastIndex(LastIndex),LastTerm(LastTerm),Peers(peers),Path(path) {
}

Snapshot::~Snapshot() {
}
int Reader(void* arg,const struct iovec *iov, int iovcnt){
	int ret;
	int nrd = 0;
	int fd = *( (int*)(arg) );
	while(true){
		ret = readv(fd, iov, iovcnt);
		if(ret < 0){
			int err = errno;
			if(err == EINTR){
				continue;
			}else{
				break;
			}
		}
		nrd = ret;
		break;
	}
	return nrd;
}
Snapshot* Snapshot::Load(const std::string& path){
	int fd = open(path.c_str(),O_RDONLY,0600);
	if(fd < 0){
		LOG(WARN) << "Open Fial fail: path:" << path << "err:" << strerror(errno);
		return NULL;
	}
	lseek(fd,0,SEEK_SET);
	abb::Buffer buf;
	buf.WriteFromeReader(Reader,&fd);
	if(buf.Size() <= 0){
		return NULL;
	}
	Snapshot* s = new Snapshot();
	try{
		s->Decode(buf);
	}catch(...){
		delete s;
		close(fd);
		return NULL;
	}
	return s;
}
bool Snapshot::Save(){
	int fd = open(Path.c_str(),O_CREAT|O_WRONLY,0600);
	if(fd < 0){
		LOG(WARN) << "Open Fial fail: path:" << Path << "err:" << strerror(errno);
		return false;
	}
	abb::Buffer buf;
	this->Encode(buf);
	while(true){
		int ret = write(fd,buf.Data(),buf.Size());
		if(ret < 0){
			if(errno == EINTR){
				continue;
			}else{
				LOG(WARN) << "Snapshot.save.fail.write: path:" << Path << "err:" << strerror(errno);
				delete buf;
				close(fd);
				return false;
			}
		}else{
			break;
		}
	}
	delete buf;
	fsync(fd);
	close(fd);
	return true;
}
void Snapshot::Encode(abb::Buffer &buf){
	buf.NET_WriteUint64(LastIndex);
	buf.NET_WriteUint64(LastTerm);
	buf.NET_WriteUint16(state.Size());
	buf.Write(state.Data(),state.Size());
	buf.NET_WriteUint16(Peers.size());
	for(int i=0;i<Peers.size();i++){
		buf << Peers[i].name;
		buf << Peers[i].addr;
	}
}
void Snapshot::Decode(abb::Buffer &buf){
	LastIndex = buf.HOST_ReadUint64();
	LastTerm = buf.HOST_ReadUint64();
	uint16_t size = buf.HOST_ReadUint16();
	state.EnoughSize(size);
	buf.Read(state.WrData(),size);
	state.GaveWr(size);
	uint16_t sz= buf.HOST_ReadUint16();
	for(int i=0;i<sz;i++){
		PeerInfo info;
		buf >> info.name;
		buf >> info.addr;
		this->Peers.push_back(info);
	}
}
void Snapshot::Remove(){
	remove(this->Path.c_str());
}
SnapshotResponce::SnapshotResponce():success(false){

}
SnapshotResponce::SnapshotResponce(bool suc):success(suc){

}
SnapshotResponce::~SnapshotResponce(){};
bool SnapshotResponce::Encode(abb::Buffer &buf){
	buf << success;
	return true;
}
bool SnapshotResponce::Decode(abb::Buffer &buf){
	buf >> success;
	return true;
}
SnapshotRequest::SnapshotRequest()
:LastIndex(0),LastTerm(0){

}
SnapshotRequest::SnapshotRequest(
		const std::string& LeaderName,
		const Snapshot& snap)
:LastIndex(snap.LastIndex),LastTerm(snap.LastTerm),LeaderName(LeaderName){

}
SnapshotRequest::~SnapshotRequest(){

}
bool SnapshotRequest::Encode(abb::Buffer &buf){
	buf.NET_WriteUint64(LastIndex);
	buf.NET_WriteUint64(LastTerm);
	buf << LeaderName;
	return true;
}
bool SnapshotRequest::Decode(abb::Buffer &buf){
	LastIndex = buf.HOST_ReadUint64();
	LastTerm = buf.HOST_ReadUint64();
	buf >> LeaderName;
	return true;
}

SnapshotRecoveryRequest::SnapshotRecoveryRequest()
:LastIndex(0),LastTerm(0){

}
SnapshotRecoveryRequest::SnapshotRecoveryRequest(
		const std::string& LeaderName,
		Snapshot& snap)
:LastIndex(snap.LastIndex),LastTerm(snap.LastTerm),LeaderName(LeaderName),Peers(snap.Peers){
	state.Write((snap.state.Data()), snap.state.Size());
}
SnapshotRecoveryRequest::~SnapshotRecoveryRequest(){

}
bool SnapshotRecoveryRequest::Encode(abb::Buffer &buf){
	buf.NET_WriteUint64(LastIndex);
	buf.NET_WriteUint64(LastTerm);
	buf.NET_WriteUint16(state.Size());
	buf.Write(state.Data(),state.Size());
	buf << LeaderName;
	buf.NET_WriteUint16(Peers.size());
	for(int i=0;i<Peers.size();i++){
		buf << Peers[i].name;
		buf << Peers[i].addr;
	}
	return true;
}
bool SnapshotRecoveryRequest::Decode(abb::Buffer &buf){
	LastIndex = buf.HOST_ReadUint64();
	LastTerm = buf.HOST_ReadUint64();
	uint16_t size = buf.HOST_ReadUint16();

	state.EnoughSize(size);
	buf.Read(state.WrData(),size);
	state.GaveWr(size);

	buf >> LeaderName;
	uint16_t sz= buf.HOST_ReadUint16();
	for(int i=0;i<sz;i++){
		PeerInfo info;
		buf >> info.name;
		buf >> info.addr;
		this->Peers.push_back(info);
	}
	return true;
}
SnapshotRecoveryResponce::SnapshotRecoveryResponce():Term(0),Success(false),CommitIndex(0){};
SnapshotRecoveryResponce::SnapshotRecoveryResponce(uint64_t Term,bool Success,uint64_t CommitIndex)
:Term(Term),Success(Success),CommitIndex(CommitIndex){};
SnapshotRecoveryResponce::~SnapshotRecoveryResponce(){

}
bool SnapshotRecoveryResponce::Encode(abb::Buffer &buf){
	buf.NET_WriteUint64(Term);
	buf.NET_WriteUint64(CommitIndex);
	buf << Success;
	return true;
}
bool SnapshotRecoveryResponce::Decode(abb::Buffer &buf){
	Term = buf.HOST_ReadUint64();
	CommitIndex = buf.HOST_ReadUint64();
	buf >>Success;
	return true;
}
} /* namespace raft */
