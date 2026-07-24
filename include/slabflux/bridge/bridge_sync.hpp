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
 * @brief  SLABFLUX Chip Core
 * @details Bridge Suite
 */

#pragma once
#include <atomic>
#include <immintrin.h>
#include <cstdint>
#include <utility>
#include <array>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/scoped_ptr.hpp"
#include "slabflux/core/managed_data.hpp"
#include "slabflux/platform/os.hpp"

 // ============================================================================
 // HFT ZERO-COST COMPILER BARRIER
 // ============================================================================
#define SLAB_COMPILER_BARRIER() std::atomic_signal_fence(std::memory_order_seq_cst)

#include "slabflux/core.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/core/conduit.hpp"

namespace slabflux::bridge {

    // ========================================================================
    // THE WIDE STATE
    // ========================================================================
    struct alignas(64) wide_state_slab {
        alignas(64) std::atomic<uint64_t> sequence{ 0 };
        alignas(64) float positions[1024];
        alignas(64) std::atomic<uint64_t> last_lsn{ 0 };

        // Ghost Tearing fix: Prevent readers from reading zeroes if they start early
        wide_state_slab() {
            for (int i = 0; i < 1024; ++i) {
                positions[i] = static_cast<float>(i) * 0.001f;
            }
        }
    };

    // ========================================================================
    // BASE TEMPLATE
    // ========================================================================
    template <typename ConduitType, typename InputType, bool NeedsSFence>
    class bridge_base {
    protected:
        ConduitType input_queue;
        wide_state_slab* shared_state;

    public:
        bridge_base() {
            void* mem = core::hardware_topology::allocate_on_local_node(sizeof(wide_state_slab));
            shared_state = new (mem) wide_state_slab();
        }

        bool try_read_wide(float * SLAB_RESTRICT out_positions, uint64_t& out_lsn) const noexcept {
            uint64_t s1 = shared_state->sequence.load(std::memory_order_acquire);
            if (s1 & 1) [[unlikely]] return false;

            SLAB_COMPILER_BARRIER();
            _mm_lfence();
            SLAB_COMPILER_BARRIER();

            const float* src = shared_state->positions;
#ifdef _MSC_VER
#pragma loop(no_vector)
#else
#pragma GCC unroll 8
#endif
            for (int i = 0; i < 64; ++i) {
                // Safe stack corruption protection
                _mm512_storeu_ps(&out_positions[i * 16], _mm512_load_ps(&src[i * 16]));
            }

            out_lsn = shared_state->last_lsn.load(std::memory_order_relaxed);

            SLAB_COMPILER_BARRIER();
            _mm_lfence();
            SLAB_COMPILER_BARRIER();

            return (s1 == shared_state->sequence.load(std::memory_order_acquire));
        }

    protected:
        SLAB_FORCE_INLINE void publish(const InputType& in, auto& logic, auto& context) noexcept {
            uint64_t lsn = context.reserve_next();
            
            uint64_t seq;
            for (uint32_t retries = 0; ; ++retries) {
                seq = shared_state->sequence.load(std::memory_order_acquire);
                if (SL_EXPECT_TRUE(!(seq & 1))) { // Even = Matrix Unlocked
                    if (SL_EXPECT_TRUE(shared_state->sequence.compare_exchange_strong(seq, seq + 1, 
                        std::memory_order_release, std::memory_order_acquire))) {
                        break; // Lock acquired
                    }
                }

                // Interconnect Stabilization: Restructured backoff with syscall abstraction.
                if (retries < 32) {
                    for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
                } else {
                    slabflux::os::futex_wait(&shared_state->sequence, seq);
                }
            }

            SLAB_COMPILER_BARRIER();

            logic.process(in, lsn, shared_state->positions);
            shared_state->last_lsn.store(lsn, std::memory_order_relaxed);

            SLAB_COMPILER_BARRIER();
            if constexpr (NeedsSFence) {
                _mm_sfence();
            }

            shared_state->sequence.store(seq + 2, std::memory_order_release); // Release lock, make even
            slabflux::os::futex_wake(&shared_state->sequence);
        }
    };

    // ========================================================================
    // CONCRETE BRIDGES
    // ========================================================================
    template <typename T, std::size_t Size = 1024>
    class spsc_data_bridge : public bridge_base<core::spsc_conduit<T, Size>, T, true> {
    public:
        void send(const T& in) noexcept { this->input_queue.push(in); }
        bool try_send(const T& in) noexcept { return this->input_queue.try_push(in); }

        void consume(auto& logic, auto& context) noexcept {
            T in;
            while (this->input_queue.try_pop(in)) { this->publish(in, logic, context); }
        }

        bool consume_one(auto& logic, auto& context) noexcept {
            T in;
            if (this->input_queue.try_pop(in)) {
                this->publish(in, logic, context);
                return true;
            }
            return false;
        }
    };

    template <typename T, std::size_t Size = 1024>
    class mpmc_data_bridge : public bridge_base<core::mpmc_conduit<T, Size>, T, false> {
    public:
        void send(const T& in) noexcept { this->input_queue.push(in); }
        bool try_send(const T& in) noexcept { return this->input_queue.try_push(in); }

        void consume(auto& logic, auto& context) noexcept {
            T in;
            while (this->input_queue.try_pop(in)) { this->publish(in, logic, context); }
        }

        bool consume_one(auto& logic, auto& context) noexcept {
            T in;
            if (this->input_queue.try_pop(in)) {
                this->publish(in, logic, context);
                return true;
            }
            return false;
        }
    };

    template <typename T, std::size_t Size = 1024>
    class spsc_event_bridge : public bridge_base<core::spsc_conduit<T*, Size>, T, true> {
        void (*deleter_)(void*, void*);
        void* pool_ctx_;
    public:
        template <typename Pool>
        explicit spsc_event_bridge(Pool& p) 
            : deleter_(Pool::deleter_fn), pool_ctx_(&p) {}

        void send(core::scoped_ptr<T>& in) noexcept { 
            if (in) this->input_queue.push(in.release()); 
        }
        bool try_send(core::scoped_ptr<T>& in) noexcept {
            if (!in) return false;
            if (this->input_queue.try_push(in.get())) { in.release(); return true; }
            return false;
        }

        void consume(auto& logic, auto& context) noexcept {
            T* raw = nullptr;
            while (this->input_queue.try_pop(raw)) {
                if (raw) {
                    this->publish(*raw, logic, context);
                    // Return memory to the originating pool
                    deleter_(pool_ctx_, raw);
                }
            }
        }

        bool consume_one(auto& logic, auto& context) noexcept {
            T* raw = nullptr;
            if (this->input_queue.try_pop(raw)) {
                if (raw) {
                    this->publish(*raw, logic, context);
                    deleter_(pool_ctx_, raw);
                }
                return true;
            }
            return false;
        }
    };

    template <typename T, std::size_t Size = 1024>
    class mpmc_event_bridge : public bridge_base<core::mpmc_conduit<T*, Size>, T, false> {
        void (*deleter_)(void*, void*);
        void* pool_ctx_;
    public:
        template <typename Pool>
        explicit mpmc_event_bridge(Pool& p) 
            : deleter_(Pool::deleter_fn), pool_ctx_(&p) {}

        void send(core::scoped_ptr<T>& in) noexcept { 
            if (in) this->input_queue.push(in.release()); 
        }
        bool try_send(core::scoped_ptr<T>& in) noexcept {
            if (!in) return false;
            if (this->input_queue.try_push(in.get())) { in.release(); return true; }
            return false;
        }

        void consume(auto& logic, auto& context) noexcept {
            T* raw = nullptr;
            while (this->input_queue.try_pop(raw)) {
                if (raw) {
                    this->publish(*raw, logic, context);
                    deleter_(pool_ctx_, raw);
                }
            }
        }

        bool consume_one(auto& logic, auto& context) noexcept {
            T* raw = nullptr;
            if (this->input_queue.try_pop(raw)) {
                if (raw) {
                    this->publish(*raw, logic, context);
                    deleter_(pool_ctx_, raw);
                }
                return true;
            }
            return false;
        }
    };

} // namespace slabflux::bridge
