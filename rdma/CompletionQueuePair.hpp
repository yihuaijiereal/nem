
#pragma once

#include <vector>
#include <cstdint>
#include <mutex>

struct ibv_comp_channel;
struct ibv_srq;
struct ibv_cq;

namespace rdma {
    class Network;

    class CompletionQueuePair {
        friend class QueuePair;

        CompletionQueuePair(CompletionQueuePair const &) = delete;

        CompletionQueuePair &operator=(CompletionQueuePair const &) = delete;

        //发送cq
        ibv_cq *sendQueue;
        ///接收cq
        ibv_cq *receiveQueue;
        
        ibv_comp_channel *channel;

        /// The cached work completions
        std::vector<std::pair<bool, uint64_t> > cachedCompletions;
        /// Protect wait for events method from concurrent access
        std::mutex guard;

        uint64_t pollCompletionQueue(ibv_cq *completionQueue, int type);

        std::pair<bool, uint64_t> waitForCompletion(bool restrict, bool onlySend);

    public:
        CompletionQueuePair(Network &network);

        ~CompletionQueuePair();

        uint64_t pollSendCompletionQueue();

        /// Poll the send completion queue with a user defined type
        uint64_t pollSendCompletionQueue(int type);

        /// Poll the receive completion queue
        uint64_t pollRecvCompletionQueue();

        // Poll a completion queue blocking
        uint64_t pollCompletionQueueBlocking(ibv_cq *completionQueue, int type);

        /// Poll the send completion queue blocking
        uint64_t pollSendCompletionQueueBlocking();

        /// Poll the receive completion queue blocking
        uint64_t pollRecvCompletionQueueBlocking();

        /// Wait for a work request completion
        std::pair<bool, uint64_t> waitForCompletion();

        uint64_t waitForCompletionSend();

        uint64_t waitForCompletionReceive();
    };

}