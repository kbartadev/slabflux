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
 * @file durable_saga_orchestrator.hpp
 * @brief Durability and Saga state management.
 */

#pragma once

#include "slabflux/io/durable_journal.hpp"
#include <atomic>
#include <cstdint>

namespace slabflux::dist {

    enum class saga_status : uint8_t { pending, committed, aborted };

    struct alignas(64) saga_context {
        uint64_t saga_id;
        saga_status status;
        uint64_t start_time_ns;
        char padding[47]; // Hardware L1 alignment
    };

    template<typename JournalType>
    class saga_manager {
        JournalType& persistence_;

    public:
        explicit saga_manager(JournalType& j) noexcept : persistence_(j) {}

        /**
         * @brief Emits a deterministic commit marker to the distributed log.
         * @param saga_id The globally unique identifier of the transaction.
         */
        SLAB_FORCE_INLINE void commit_global_state(uint64_t saga_id) noexcept {

            // Hardware memory barrier: Ensures all preceding payload modifications
            // to the state machine are globally visible before we write the commit marker.
            std::atomic_thread_fence(std::memory_order_release);

            // Zero-copy allocation directly on the NVMe map
            auto* marker = persistence_.reserve_slot();

            if (__builtin_expect(marker != nullptr, 1)) [[likely]] {
                marker->saga_id = saga_id;
                marker->status = saga_status::committed;
                // Relying on TSC (Time Stamp Counter) for sub-nanosecond precision
                marker->start_time_ns = __rdtsc();

                persistence_.commit_slot();
            }
        }

        SLAB_FORCE_INLINE void abort_global_state(uint64_t saga_id) noexcept {
            std::atomic_thread_fence(std::memory_order_release);
            auto* marker = persistence_.reserve_slot();
            if (__builtin_expect(marker != nullptr, 1)) [[likely]] {
                marker->saga_id = saga_id;
                marker->status = saga_status::aborted;
                marker->start_time_ns = __rdtsc();
                persistence_.commit_slot();
            }
        }
    };
} // namespace slabflux::dist
