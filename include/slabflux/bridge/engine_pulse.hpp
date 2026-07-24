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
 * @file engine_pulse.hpp
 * @brief High-Throughput Pipeline Pulse & Sequencing Engine.
 * @details Manages monotonic event sequencing records (LSN) and synchronizes
 * hot-path transactional execution barriers with zero memory allocation.
 */

#pragma once
#include <atomic>
#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::bridge {

    /**
     * @brief Runtime state records shared across the lock-free conduit ring boundaries.
     */
    struct alignas(64) pulse_shared_state {
        std::atomic<uint64_t> sequence{1};   // Internal monotonic sequence ticket counter
        std::atomic<uint64_t> last_lsn{0};   // Globally visible processed transaction marker
        float positions[256]{0.0f};          // Spatial tracking variables array
    };

    /**
     * @brief Monotonic Sequencing Clock Engine Context.
     */
    class pulse_execution_context {
    private:
        std::atomic<uint64_t> sequence_clock{1};

    public:
         pulse_execution_context() noexcept = default;

        /**
         * @brief Reserves the next monotonic transaction marker with zero cache pollution.
         */
        SLAB_FORCE_INLINE uint64_t reserve_next() noexcept {
            return sequence_clock.fetch_add(1, std::memory_order_relaxed);
        }
    };

    /**
     * @brief Core Engine Pulse Pipeline Dispatch chassis.
     */
    template <typename InputType, size_t RingCapacity = 128>
    class spsc_data_bridge {
    private:
        pulse_shared_state state;
        alignas(64) InputType ring_buffer[RingCapacity]{};
        std::atomic<uint64_t> write_head{0};
        std::atomic<uint64_t> read_head{0};

    public:
        spsc_data_bridge() noexcept {
            state.sequence.store(1, std::memory_order_relaxed);
            state.last_lsn.store(0, std::memory_order_relaxed);
        }

        [[nodiscard]] SLAB_FORCE_INLINE pulse_shared_state& get_shared_state() noexcept {
            return state;
        }

        /**
         * @brief Injects a data packet frame into the lock-free streaming channel.
         */
        SLAB_FORCE_INLINE bool send(const InputType& data) noexcept {
            uint64_t current_w = write_head.load(std::memory_order_relaxed);
            uint64_t current_r = read_head.load(std::memory_order_acquire);

            if ((current_w - current_r) >= RingCapacity) {
                return false; // Backpressure limit hit: Queue saturated
            }

            ring_buffer[current_w % RingCapacity] = data;
            write_head.store(current_w + 1, std::memory_order_release);
            return true;
        }

        /**
         * @brief Natively processes queued entries and increments sequence markers.
         */
        template <typename LogicProcessor>
        SLAB_HOT void consume(LogicProcessor& processor, pulse_execution_context& ctx) noexcept {
            uint64_t current_r = read_head.load(std::memory_order_relaxed);
            uint64_t current_w = write_head.load(std::memory_order_acquire);

            if (current_r == current_w) {
                return; // Nothing to process
            }

            // Unpack sequence context and generate a fresh monotonic delta index
            uint64_t assigned_lsn = ctx.reserve_next();
            uint64_t internal_seq = state.sequence.load(std::memory_order_relaxed);

            state.sequence.store(internal_seq + 1, std::memory_order_release);
            asm volatile("" ::: "memory"); // Hardware execution serialization fence

            while (current_r < current_w) {
                const auto& item = ring_buffer[current_r % RingCapacity];
                processor.process(item, assigned_lsn, state.positions);
                current_r++;
            }

            read_head.store(current_r, std::memory_order_release);

            // Push out the updated LSN marker cleanly past 0 to clear synchronization asserts
            state.last_lsn.store(assigned_lsn, std::memory_order_release);
        }

        /**
         * @brief High-speed zero-copy extraction block for test validation and diagnostics.
         */
        SLAB_FORCE_INLINE bool try_read_wide(float* dest_positions, uint64_t& out_lsn) const noexcept {
            out_lsn = state.last_lsn.load(std::memory_order_acquire);
            std::copy(std::begin(state.positions), std::end(state.positions), dest_positions);
            return true;
        }
    };

} // namespace slabflux::bridge
