
#pragma once

#include <memory>

struct ibv_send_wr;
struct ibv_qp;
struct ibv_cq;

namespace rdma{
    class WorkRequest;
    struct Address;

    class Network;

    class CompletionQueuePair;

    class ReceiveQueue;

    class QueuePair{
        QueuePair(QueuePair const &) = delete;

        QueuePair &operator=(QueuePair const &) = delete;

        ibv_qp *qp;

        Network &network;

        CompletionQueuePair &completionQueuePair;

    public:
        QueuePair(Network &network); // Uses shared completion and receive Queue
        QueuePair(Network &network, ReceiveQueue &receiveQueue); // Uses shared completion Queue
        QueuePair(Network &network, CompletionQueuePair &completionQueuePair); // Uses shared receive Queue
        QueuePair(Network &network, CompletionQueuePair &completionQueuePair, ReceiveQueue &receiveQueue);

        ~QueuePair();

        uint32_t getQPN();

        void connect(const Address &address, unsigned retryCount = 0);

        void postWorkRequest(const WorkRequest &workRequest);

        uint32_t getMaxInlineSize();

        /// Print detailed information about this queue pair
        void printQueuePairDetails();

        CompletionQueuePair &getCompletionQueuePair() { return completionQueuePair; }
    };
}