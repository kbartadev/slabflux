/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * @file ndp_matrix.hpp
 * @brief Lock-Free O(1) Zero-Allocation NDP Resolution Table for IPv6.
 */

#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>
#include <immintrin.h>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::net {

    struct alignas(64) ndp_entry {
        std::atomic<uint64_t> ip_high{0};
        std::atomic<uint64_t> ip_low{0};
        uint8_t mac[6]{0};
        uint16_t _pad1{0};
        std::atomic<uint32_t> seqlock{0};
        std::atomic<uint64_t> last_updated_ms{0};
        uint32_t _pad2[4]{0};
    };

    static_assert(sizeof(ndp_entry) == 64, "NDP Entry must tile exactly to one L1 Cache Line");

    template<size_t Capacity = 4096>
    class alignas(64) ndp_matrix {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be an exact power of two");
        ndp_entry table_[Capacity];

        static inline uint32_t hash(uint64_t high, uint64_t low) noexcept {
            uint64_t mix = high ^ low;
            return static_cast<uint32_t>((mix >> 32) ^ mix);
        }

    public:
        ndp_matrix() = default;

        SLAB_HOT void insert(const uint64_t ip[2], const uint8_t* mac, uint64_t now_ms) noexcept {
            if (ip[0] == 0 && ip[1] == 0) return;
            uint32_t h = hash(ip[0], ip[1]) & (Capacity - 1);
            
            for (size_t i = 0; i < 8; ++i) {
                uint32_t idx = (h + i) & (Capacity - 1);
                uint64_t expected_high = table_[idx].ip_high.load(std::memory_order_relaxed);
                uint64_t expected_low = table_[idx].ip_low.load(std::memory_order_relaxed);
                
                if ((expected_high == ip[0] && expected_low == ip[1]) || (expected_high == 0 && expected_low == 0)) {
                    uint32_t seq = table_[idx].seqlock.load(std::memory_order_relaxed);
                    table_[idx].seqlock.store(seq + 1, std::memory_order_release);
                    
                    std::memcpy(table_[idx].mac, mac, 6);
                    table_[idx].last_updated_ms.store(now_ms, std::memory_order_relaxed);
                    
                    table_[idx].ip_high.store(ip[0], std::memory_order_relaxed);
                    table_[idx].ip_low.store(ip[1], std::memory_order_release);
                    table_[idx].seqlock.store(seq + 2, std::memory_order_release);
                    return;
                }
            }
        }

        SLAB_HOT bool resolve(const uint64_t ip[2], uint8_t* out_mac) const noexcept {
            if (ip[0] == 0 && ip[1] == 0) return false;
            uint32_t h = hash(ip[0], ip[1]) & (Capacity - 1);
            
            for (size_t i = 0; i < 8; ++i) {
                uint32_t idx = (h + i) & (Capacity - 1);
                if (table_[idx].ip_high.load(std::memory_order_acquire) == ip[0] &&
                    table_[idx].ip_low.load(std::memory_order_acquire) == ip[1]) {
                    uint32_t seq1, seq2;
                    do {
                        seq1 = table_[idx].seqlock.load(std::memory_order_acquire);
                        if (seq1 & 1) { _mm_pause(); continue; }
                        
                        std::memcpy(out_mac, table_[idx].mac, 6);
                        
                        seq2 = table_[idx].seqlock.load(std::memory_order_acquire);
                    } while (seq1 != seq2 || (seq1 & 1));
                    return true;
                }
            }
            return false;
        }
    };
}