SET ( RAFT_SRC 
	 raft/append_entries.cpp
	 raft/append_entries.hpp
	 raft/commond.cpp
	 raft/commonds.cpp
	 raft/messages.cpp
	 raft/event.hpp
	 raft/http_translate.cpp
	 raft/http_translate.hpp
	 raft/i_translate.hpp
	 raft/log.cpp
	 raft/log.hpp
	 raft/log_entry.cpp
	 raft/log_entry.hpp
	 raft/monitor.hpp
	 raft/monitor.cpp
	 raft/peer.cpp
	 raft/peer.hpp
	 raft/server.hpp
	 raft/server.cpp
	 raft/snapshot.cpp
	 raft/snapshot.hpp
	 raft/vote.hpp
	 raft/vote.cpp
	)

ADD_LIBRARY(raft SHARED ${RAFT_SRC})

ADD_LIBRARY(raft_static STATIC ${RAFT_SRC})
SET_TARGET_PROPERTIES(raft_static PROPERTIES OUTPUT_NAME "raft")
SET_TARGET_PROPERTIES(raft PROPERTIES CLEAN_DIRECT_OUTPUT 1)
SET_TARGET_PROPERTIES(raft_static PROPERTIES CLEAN_DIRECT_OUTPUT 1)


TARGET_LINK_LIBRARIES(raft abb)
TARGET_LINK_LIBRARIES(raft_static abb)

INSTALL( TARGETS raft;raft_static
         RUNTIME DESTINATION bin
         LIBRARY DESTINATION lib
         ARCHIVE DESTINATION lib
)