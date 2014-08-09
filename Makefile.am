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
					src/raft/server.cpp\
					src/raft/raft_log.cpp\
					src/raft/commonds.cpp


#libraft_la_LIBADD=-lpthread -lrt -labb

raftincludedir=$(includedir)/raft
raftinclude_HEADS=$(top_srcdir)/include/raft/commond.hpp\
					$(top_srcdir)/include/raft/commonds.hpp\
					$(top_srcdir)/include/raft/i_message.hpp\
					$(top_srcdir)/include/raft/i_stat_machine.hpp\
					$(top_srcdir)/include/raft/i_translate.hpp\
					$(top_srcdir)/include/raft/server.hpp
