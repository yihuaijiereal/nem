
#include "ReceiveQueue.hpp"
#include "WorkRequest.hpp"
#include "Network.hpp"

#include <infiniband/verbs.h>
#include <cstring>
#include <iostream>
#include <iomanip>

using namespace std;

namespace rdma {
    ReceiveQueue::ReceiveQueue(Network &network)
    {
        // Create receive queue
        struct ibv_srq_init_attr srq_init_attr{};
        memset(&srq_init_attr, 0, sizeof(srq_init_attr));
        srq_init_attr.attr.max_wr = 16351;
        srq_init_attr.attr.max_sge = 1;
        queue = ibv_create_srq(network.protectionDomain, &srq_init_attr);
        if (!queue) {
            string reason = "could not create receive queue";
            cerr << reason << endl;
            throw NetworkException(reason);
        }
    }

    ReceiveQueue::~ReceiveQueue()
    {
        int status;

        // Destroy the receive queue
        status = ::ibv_destroy_srq(queue);
        if (status != 0) {
            string reason = "destroying the receive queue failed with error " + to_string(errno) + ": " + strerror(errno);
            cerr << reason << endl;
            throw NetworkException(reason);
        }
    }
}