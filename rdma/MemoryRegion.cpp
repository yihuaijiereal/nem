
#include "MemoryRegion.hpp"
#include "Network.hpp"

#include <infiniband/verbs.h>
#include <iostream>
#include <cstring>

using namespace std;

namespace rdma {
    int convertPermissions(MemoryRegion::Permission permissions) {
        int flags = 0;
        if (static_cast<underlying_type<MemoryRegion::Permission>::type>(permissions &
                                                                         MemoryRegion::Permission::LocalWrite)) {
            flags |= IBV_ACCESS_LOCAL_WRITE;
        }
        if (static_cast<underlying_type<MemoryRegion::Permission>::type>(permissions &
                                                                         MemoryRegion::Permission::RemoteWrite)) {
            flags |= IBV_ACCESS_REMOTE_WRITE;
        }
        if (static_cast<underlying_type<MemoryRegion::Permission>::type>(permissions &
                                                                         MemoryRegion::Permission::RemoteRead)) {
            flags |= IBV_ACCESS_REMOTE_READ;
        }
        if (static_cast<underlying_type<MemoryRegion::Permission>::type>(permissions &
                                                                         MemoryRegion::Permission::RemoteAtomic)) {
            flags |= IBV_ACCESS_REMOTE_ATOMIC;
        }
        if (static_cast<underlying_type<MemoryRegion::Permission>::type>(permissions &
                                                                         MemoryRegion::Permission::MemoryWindowBind)) {
            flags |= IBV_ACCESS_MW_BIND;
        }
        return flags;
    }

    //---------------------------------------------------------------------------
    MemoryRegion::MemoryRegion(void *address, size_t size, ibv_pd *protectionDomain, Permission permissions) : address(
            address), size(size) {
        key = ::ibv_reg_mr(protectionDomain, address, size, convertPermissions(permissions));
        if (key == nullptr) {
            string reason = "registering memory failed with error " + to_string(errno) + ": " + strerror(errno);
            cerr << reason << endl;
            throw NetworkException(reason);
        }
    }

    //---------------------------------------------------------------------------
    MemoryRegion::~MemoryRegion() {
        if (::ibv_dereg_mr(key) != 0) {
            string reason = "deregistering memory failed with error " + to_string(errno) + ": " + strerror(errno);
            cerr << reason << endl;
            throw NetworkException(reason);
        }
    }

    //---------------------------------------------------------------------------
    MemoryRegion::Slice MemoryRegion::slice(size_t offset, size_t size) {
        return MemoryRegion::Slice(reinterpret_cast<uint8_t *>(address) + offset, size, key->lkey);
    }

    //---------------------------------------------------------------------------
    ostream &operator<<(ostream &os, const MemoryRegion &memoryRegion) {
        return os << "ptr=" << memoryRegion.address << " size=" << memoryRegion.size << " key={..}";
    }
}