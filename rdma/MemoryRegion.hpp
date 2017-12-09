
#pragma once

#include <stdint.h>
#include <cstdlib>
#include <type_traits>
#include <ostream>

struct ibv_mr;
struct ibv_pd;

namespace rdma {
    class MemoryRegion{
    public:
        enum class Permission : uint8_t {
            None = 0,
            LocalWrite = 1 << 0,
            RemoteWrite = 1 << 1,
            RemoteRead = 1 << 2,
            RemoteAtomic = 1 << 3,
            MemoryWindowBind = 1 << 4,
            All = LocalWrite | RemoteWrite | RemoteRead | RemoteAtomic | MemoryWindowBind
        };

        struct Slice {
            friend class MemoryRegion;

            void *address;
            size_t size;
            uint32_t lkey;
            Slice(void *address, size_t size, uint32_t lkey) : address(address), size(size), lkey(lkey) {}
        };

        ibv_mr *key;
        void *address;
        const size_t size;

        MemoryRegion(void *address, size_t size, ibv_pd *protectionDomain, Permission permissions);

        ~MemoryRegion();

        Slice slice(size_t offset, size_t size);

        MemoryRegion(MemoryRegion const &) = delete;
        void operator=(MemoryRegion const &) = delete;
    };

    inline MemoryRegion::Permission operator|(MemoryRegion::Permission a, MemoryRegion::Permission b) {
        return static_cast<MemoryRegion::Permission>(static_cast<std::underlying_type<MemoryRegion::Permission>::type>(a) | static_cast<std::underlying_type<MemoryRegion::Permission>::type>(b));
    }

    inline MemoryRegion::Permission operator&(MemoryRegion::Permission a, MemoryRegion::Permission b) {
        return static_cast<MemoryRegion::Permission>(static_cast<std::underlying_type<MemoryRegion::Permission>::type>(a) & static_cast<std::underlying_type<MemoryRegion::Permission>::type>(b));
    }
    std::ostream &operator<<(std::ostream& os, const MemoryRegion& memoryRegion);
}