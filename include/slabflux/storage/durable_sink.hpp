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
#include "slabflux/io/durable_journal.hpp"

namespace slabflux::storage {

    template <typename EventType, size_t JournalSizeBytes = 1073741824>
    class alignas(64) durable_sink_base { // Renamed to base class
        slabflux::io::durable_journal<EventType, JournalSizeBytes> journal_; // Default to mmap journal
        
    public:
        explicit durable_sink_base(const char* filepath) noexcept : journal_(filepath) {}

        [[nodiscard]] uint8_t* get_arena_base() const noexcept {
            return journal_.get_arena_base();
        }

        /**
         * @brief ABSOLUTE ZERO-COPY INGEST
         * The network or business logic requests memory FROM HERE, not from the heap!
         */
        [[nodiscard]] EventType* reserve_slot() noexcept {
            return journal_.reserve_slot();
        }

        /**
         * @brief Resets the arena pointers to support continuous benchmarking.
         * @details Executes with relaxed memory order as it is an out-of-band operation.
         */
        SLAB_FORCE_INLINE void reset() noexcept {
            journal_.reset();
        }

        /** @brief Published Commitment. */
        void commit() noexcept { journal_.commit_slot(); }
    };

    // New template for durable_sink that allows specifying the JournalType
    template <typename EventType, typename JournalType, size_t JournalSizeBytes = 1073741824>
    class alignas(64) durable_sink {
        JournalType journal_;
        
    public:
        // Constructor for io_uring with SQ_AFF
        explicit durable_sink(const char* filepath, int sq_core_id) noexcept : journal_(filepath, sq_core_id) {}
        // Overload for basic mmap journal (no SQ_AFF)
        explicit durable_sink(const char* filepath) noexcept : journal_(filepath) {}

        [[nodiscard]] uint8_t* get_arena_base() const noexcept { return journal_.get_arena_base(); }
        [[nodiscard]] EventType* reserve_slot() noexcept { return journal_.reserve_slot(); }
        SLAB_FORCE_INLINE void reset() noexcept { journal_.reset(); }
        void commit() noexcept { journal_.commit_slot(); }
        // Expose force_flush if the underlying journal supports it
        template<typename J = JournalType>
        auto force_flush() noexcept -> decltype(std::declval<J>().force_flush()) {
            return journal_.force_flush();
        }
    };
}
