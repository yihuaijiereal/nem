#ifndef RDMA_EC_BUFFER_H
#define RDMA_EC_BUFFER_H

#include <atomic>
#include <vector>
#include "rdma/Network.hpp"
#include "rdma/CompletionQueuePair.hpp"
#include "rdma/QueuePair.hpp"
#include "rdma/MemoryRegion.hpp"
#include "rdma/ECCalc.hpp"

struct RDMANetworking {
    rdma::Network network;
    rdma::CompletionQueuePair completionQueue;
    rdma::QueuePair **queuePairs;
    int qp_num;

    /// Exchange the basic RDMA connection info for the network and queues
    RDMANetworking(int sock, int qp_num);
    ~RDMANetworking();
};

class ECBuffer{
public:
    void send(const uint8_t *data, size_t length);

    void send(const uint8_t *data, size_t length, bool inln);

    std::vector<uint8_t> receive();

    //size_t receive(void *whereTo, size_t maxSize);

    ECBuffer(size_t size, int sock, int k, int m, int w, int block_size);
    ~ECBuffer();

    bool hasData() const;
private:
    int k;
    int m;
    int w;
    int block_size;
    int blocks_all = 0;
    int read_qp = 0;
    size_t message_number = 0;

    std::unique_ptr<rdma::ECCalc> calc;

    const size_t size;
    RDMANetworking net;
    std::unique_ptr<volatile uint8_t[]> receiveBuffer;//localReceive
    std::unique_ptr<uint8_t[]> sendBuffer;//localSend
    std::unique_ptr<uint8_t[]> encode_extra_buffer;//编码的时候用来暂存不连续的块
    std::unique_ptr<uint8_t[]> decode_extra_buffer;

    std::atomic<size_t> readPos{0};//localReadPos
    size_t sendPos = 0;
    //size_t pktPos = 0;
    volatile size_t currentRemoteReceive = 0;//localCurrentRemoteReceive

    rdma::MemoryRegion localSend;
    rdma::MemoryRegion localReceive;
    rdma::MemoryRegion encodeExtra;
    rdma::MemoryRegion decodeExtra;
    rdma::MemoryRegion localReadPos;
    rdma::MemoryRegion localCurrentRemoteReceive;
    rdma::RemoteMemoryRegion remoteReceive;//remote receiveBuffer
    rdma::RemoteMemoryRegion remoteReadPos;//remote readPos

    void writeToSendBuffer(const uint8_t *data, size_t sizeToWrite);
    void writeBackToReceiveBuffer(const uint8_t *data, size_t sizeToWrite, size_t beginPos);

    void readFromReceiveBuffer(size_t readPos, uint8_t *whereTo, size_t sizeToRead) const;

    void zeroReceiveBuffer(size_t beginReceiveCount, size_t sizeToZero);
    void zeroSendBuffer(size_t sizeToZero);

    void writeToSendExtra(size_t pos);
};


#endif