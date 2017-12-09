
#pragma once

#include <vector>
#include <cstdint>
#include <mutex>

struct ibv_srq;

namespace rdma {
    class Network;

    class ReceiveQueue {
        friend class QueuePair;

        friend class CompletionQueuePair;

        ReceiveQueue(ReceiveQueue const &) = delete;

        ReceiveQueue &operator=(ReceiveQueue const &) = delete;

        /// The receive queue
        ibv_srq *queue;
    public:
        /// Ctor
        ReceiveQueue(Network &network);

        ~ReceiveQueue();
    };
}