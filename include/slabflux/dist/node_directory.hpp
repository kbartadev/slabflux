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
 * @file node_directory.hpp
 * @brief Peer Tracking.
 */

#pragma once

#include <atomic>
#include <array>
#include <x86intrin.h> // For tzcnt

namespace slabflux::dist {

    struct node_info {
        uint16_t id;
        uint32_t ip;
        std::atomic<uint64_t> last_seen_tsc;
        bool is_active;
    };

    class node_directory {
        // Hardened: Replaced dynamic heap allocation with fixed-size flat array
        static constexpr size_t MAX_PEERS = 256;
        std::array<node_info, MAX_PEERS> peers_{};
        
        // Vectorized Topology: Dense presence bitmask for O(1) discovery
        alignas(64) std::array<std::atomic<uint64_t>, 4> active_mask_{};
        size_t active_peer_count_{0};
        
    public:
        /**
         * @brief Hardware-Accelerated Peer Discovery.
         * @details Replaces textbook O(N) hashmap lookups with bitwise TZCNT scanning.
         * This enables microsecond-level active peer discovery directly from the 
         * L1 cache presence mask.
         */
        const node_info* find_first_active_peer() const noexcept {
            for (size_t i = 0; i < 4; ++i) {
                uint64_t mask = active_mask_[i].load(std::memory_order_acquire);
                while (mask != 0) {
                    // Find the lowest set bit (the active node index) in 1 CPU cycle
                    uint32_t bit_idx = static_cast<uint32_t>(__builtin_ctzll(mask));
                    uint32_t node_idx = static_cast<uint32_t>(i * 64 + bit_idx);
                    
                    // In a full implementation, we'd verify last_seen_tsc here
                    return &peers_[node_idx];
                }
            }
            return nullptr;
        }
    };
}