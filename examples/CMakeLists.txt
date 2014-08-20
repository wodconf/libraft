ADD_EXECUTABLE(test_raft test_server.cpp)
TARGET_LINK_LIBRARIES(test_raft raft;abb)