/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 * SOURCE-AVAILABLE CODEBASE
 *
 * This source file is distributed under the conditions of the SLABFLUX 
 * SOURCE-AVAILABLE AND ECOSYSTEM LICENSE (the "License").
 *
 * ----------------------------------------------------------------------------
 * CRITICAL WARNING
 * ----------------------------------------------------------------------------
 * This module may execute outside standard OS mediation layers. Incorrect 
 * integration, misconfiguration, or unsafe deployment can result in:
 *
 *   • irreversible data corruption
 *   • kernel instability or panics
 *   • NIC or PCIe bus desynchronization
 *   • undefined hardware state transitions
 *   • permanent loss of system integrity
 *
 * Use only in controlled environments with full understanding of the 
 * architectural constraints and hardware implications.
 *
 * ----------------------------------------------------------------------------
 * USAGE GUIDELINES
 * ----------------------------------------------------------------------------
 * Execution, integration, and deployment by developers is permitted strictly 
 * subject to the conditional grants and structural limitations defined within 
 * the License. Please refer to the License for full terms regarding corporate 
 * deployment and replication.
 *
 * ----------------------------------------------------------------------------
 * LIMITATION OF LIABILITY
 * ----------------------------------------------------------------------------
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, IN NO EVENT SHALL THE AUTHOR OR 
 * COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, 
 * WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF, 
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ----------------------------------------------------------------------------
 * DISCLAIMER OF WARRANTY
 * ----------------------------------------------------------------------------
 * THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *
 * See accompanying LICENSE and NOTICE files for the integrated terms of use.
 * ============================================================================*
 * @file arp_matrix.hpp
 * @brief Lock-Free O(1) Zero-Allocation ARP Resolution Table.
 */

#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>
#include <immintrin.h>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::net {

    // Seqlock-protected L2 resolution entry mapped exactly to 64 bytes
    struct alignas(64) arp_entry {
        std::atomic<uint32_t> ip{0};               // 4 bytes
        uint8_t mac[6]{0};                         // 6 bytes
        uint16_t _pad1{0};                         // 2 bytes
        std::atomic<uint32_t> seqlock{0};          // 4 bytes
        std::atomic<uint64_t> last_updated_ms{0};  // 8 bytes
        uint32_t _pad2[10]{0};                     // 40 bytes
    };

    static_assert(sizeof(arp_entry) == 64, "ARP Entry must tile exactly to one L1 Cache Line");

    template<size_t Capacity = 4096>
    class alignas(64) arp_matrix {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be an exact power of two");
        arp_entry table_[Capacity];

    public:
        arp_matrix() = default;

        /**
         * @brief Non-blocking, branch-minimal insertion via Seqlock protection.
         */
        SLAB_HOT void insert(uint32_t ip, const uint8_t* mac, uint64_t now_ms) noexcept {
            if (ip == 0) return;
            uint32_t hash = ip & (Capacity - 1);
            
            // Bounded linear probing keeps operations strictly O(1)
            for (size_t i = 0; i < 8; ++i) {
                uint32_t idx = (hash + i) & (Capacity - 1);
                uint32_t expected_ip = table_[idx].ip.load(std::memory_order_relaxed);
                
                if (expected_ip == ip || expected_ip == 0) {
                    uint32_t seq = table_[idx].seqlock.load(std::memory_order_relaxed);
                    table_[idx].seqlock.store(seq + 1, std::memory_order_release);
                    
                    std::memcpy(table_[idx].mac, mac, 6);
                    table_[idx].last_updated_ms.store(now_ms, std::memory_order_relaxed);
                    
                    table_[idx].ip.store(ip, std::memory_order_release);
                    table_[idx].seqlock.store(seq + 2, std::memory_order_release);
                    return;
                }
            }
        }

        SLAB_HOT bool resolve(uint32_t ip, uint8_t* out_mac) const noexcept {
            if (ip == 0) return false;
            uint32_t hash = ip & (Capacity - 1);
            
            for (size_t i = 0; i < 8; ++i) {
                uint32_t idx = (hash + i) & (Capacity - 1);
                if (table_[idx].ip.load(std::memory_order_acquire) == ip) {
                    uint32_t seq1, seq2;
                    do {
                        seq1 = table_[idx].seqlock.load(std::memory_order_acquire);
                        if (seq1 & 1) { _mm_pause(); continue; } // Spin if write-in-progress
                        
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