cmake_minimum_required(VERSION 3.0)
project(rdma_sockets)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wall -Wextra -Wnon-virtual-dtor -Wold-style-cast -fshow-column -pipe -march=native -I /usr/include/jerasure")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb3 -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -flto -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -libverbs -lgf_complete -lJerasure -lpthread -lrdmacm")

set(SOURCE_FILES
        rdma/CompletionQueuePair.cpp
        rdma/MemoryRegion.cpp
        rdma/Network.cpp
        rdma/QueuePair.cpp
        rdma/ReceiveQueue.cpp
        rdma/WorkRequest.cpp
        rdma/head.cpp
        rdma/ECCalc.cpp
        tcpWrapper.cpp
        ECBuffer.cpp
        RDMAMessageBuffer.cpp
        )

include_directories(..)

add_executable(ecPingPong rdmaPingPong.cpp ${SOURCE_FILES})
target_link_libraries(ecPingPong ibverbs)

add_executable(pingPong pingPong.cpp ${SOURCE_FILES})
target_link_libraries(pingPong ibverbs)

