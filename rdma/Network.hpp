
#pragma once

#include <mutex>
#include <stdexcept>
#include <vector>
#include <memory>

struct ibv_comp_channel;
struct ibv_context;
struct ibv_cq;
struct ibv_device;
struct ibv_mr;
struct ibv_pd;
struct ibv_qp;
struct ibv_srq;
union ibv_gid;

namespace rdma{

    class WorkRequest;

    class MemoryRegion;

    class CompletionQueuePair;

    class ReceiveQueue;

    class ECCalc;

    //网络异常
    class NetworkException : public std::runtime_error {
    public:
        NetworkException(const std::string &reason)
            : std::runtime_error(reason) {}
    };

    struct RemoteMemoryRegion {
        uintptr_t address;

        uint32_t key;

        RemoteMemoryRegion() = default;

        RemoteMemoryRegion(uintptr_t address, uint32_t key) : address(address), key(key) {}

        RemoteMemoryRegion slice(size_t offset);
    };

    std::ostream &operator<<(std::ostream &os, const RemoteMemoryRegion &remoteMemoryRegion);

    //LID和QPN唯一确定一个qp
    struct Address {
        uint32_t qpn;
        uint16_t lid;
        char     gid[33];
    };

    std::ostream &operator<<(std::ostream &os, const Address &address);

    //全局rdma context的抽象
    class Network{
        friend class CompletionQueuePair;

        friend class ReceiveQueue;

        friend class QueuePair;

        friend class ECCalc;

        static const int CQ_SIZE = 100;

        uint8_t ibport;

        ibv_device **devices;

        ibv_context *context;

        ibv_pd *protectionDomain;

        std::unique_ptr<CompletionQueuePair> sharedCompletionQueuePair;
        std::unique_ptr<ReceiveQueue> sharedReceiveQueue;

    public:
        Network();

        ~Network();

        uint16_t getLID();

        struct ibv_context *get_context(){return context;}

        void getsgid(Address &address);

        ibv_pd *getProtectionDomain() { return protectionDomain; }

        void printCapabilities();
    };
}