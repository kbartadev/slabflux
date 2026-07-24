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
 * ============================================================================*/

#pragma once
#include <atomic>
#include <cstdint>
#include <array>
#include <immintrin.h> // For _mm_pause
#include "slabflux/platform/os.hpp"

namespace slabflux::core {

    /**
     * @brief The LSN Engine (Global Clock).
     * @details Manages the allocated and committed event horizon.
     */
    struct alignas(64) sf_node_ctx {
        std::atomic<uint64_t> current_lsn{ 0 };
        std::atomic<uint64_t> committed_lsn{ 0 };

        // Physical routing table for precise NACK fulfillment
        std::array<slabflux::os::socket_t, 256> peer_fds;

        // Worker affinity table to support parallel LSN reservation analytics
        std::array<int, 256> worker_cores;

        sf_node_ctx() {
            peer_fds.fill(SLAB_INVALID_SOCKET);
            worker_cores.fill(-1);
        }

        // O(1) LSN reservation
        inline uint64_t reserve_next() noexcept {
            // Grade: acq_rel ensures that reservation acts as a barrier 
            // for any data being prepared for this sequence number across cores.
            return current_lsn.fetch_add(1, std::memory_order_acq_rel);
        }

        // Replicate on every 64th event
        inline bool should_broadcast(uint64_t lsn) const noexcept {
            return (lsn & 0x3F) == 0;
        }

        // Record the sanctified reality
        inline void commit(uint64_t lsn) noexcept {
            committed_lsn.store(lsn, std::memory_order_release);
        }

        inline uint64_t horizon() const noexcept {
            return committed_lsn.load(std::memory_order_acquire);
        }

        /**
         * @brief Maps a node ID to its physical file descriptor.
         * @return The socket FD or SLAB_INVALID_SOCKET if not bound.
         */
        [[nodiscard]] inline slabflux::os::socket_t get_peer_fd(uint32_t node_id) const noexcept {
            return (node_id < 256) ? peer_fds[node_id] : SLAB_INVALID_SOCKET;
        }

        /**
         * @brief Registers a worker thread's CPU affinity.
         * @details Used to track and optimize parallel LSN reservation flows.
         */
        inline void register_active_worker(uint32_t worker_id, int cpu_id) noexcept {
            if (worker_id < 256) {
                worker_cores[worker_id] = cpu_id;
            }
        }
    };

} // namespace slabflux::core
