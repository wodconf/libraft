INCLUDES=-I./include -I./src

AM_LDFLAGS=-fPIC
AM_CPPFLAGS= -fPIC 
#-march=i686
lib_LTLIBRARIES=libraft.la

libraft_la_SOURCES=src/raft/vote.cpp\
										src/raft/append_entries.cpp\
										src/raft/commond.cpp\
										src/raft/peer.cpp\
										src/raft/log_entry.cpp\
										src/raft/snapshot.cpp\
											src/raft/raft_server.cpp


#libraft_la_LIBADD=-lpthread -lrt -labb

raftincludedir=$(includedir)/raft
raftinclude_HEADS=$(top_srcdir)/include/raft/i_server.hpp
