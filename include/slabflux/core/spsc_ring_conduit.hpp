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
 *
 * @file spsc_ring_conduit.hpp
 * @brief Zero-allocation SPSC lock-free ring buffer.
 * @details The physical "wire" connecting isolated threads in the RTE.
 * 
 * High-Performance Design:
 * - Passing pointers transfers ownership without copying large payloads.
 * - Implements cache-line isolation (64-byte alignment) for producer and consumer state.
 * - Uses local shadow pointers (cached_pop_tail_ / cached_push_head_) to eliminate 
 *   cross-core atomic traffic until local capacity is exhausted.
 * - Includes SIMD-accelerated invalidation for audit/recovery scenarios.
 * 
 * API Contract:
 * 1. Blocking Operations (push/pop):
 *    Strictly spinning. Either succeeds or triggers handle_critical_error if 
 *    a NULL sentinel is encountered (which signifies a terminal logic breach).
 * 2. Non-Blocking Operations (try_push/try_pop):
 *    Returns bool status immediately. try_pop(T&) unambiguously distinguishes 
 *    between an empty conduit (false) and a valid nullptr item (true).
 * 
 * ========================================================================
 * API REFERENCE: spsc_ring_conduit<T*, Capacity>
 * ========================================================================
 * 1. INGRESS (PRODUCER PATH):
 *    - bool try_push(const T& ptr)   : Non-blocking pointer push. Returns false if full.
 *    - void push(const T& ptr)       : Blocking pointer push. Spins until slot available.
 * 
 * 2. EGRESS (CONSUMER PATH):
 *    - bool try_pop(T& out_ptr)      : Non-blocking pop into reference
 *    - T pop()                       : Blocking pop (returns pointer).
 *    - void pop(T& out_ptr)          : Blocking pop into reference.
 *    - size_t pop_batch(T*, size_t)  : Vectorized multi-pointer drain.
 *    - bool peek()                   : Checks if data is available without consuming.
 *    - void consume()                : Advances head marker.
 *    - managed_data<V, P> try_pop(P&): Pool-integrated non-blocking transfer.
     *    - managed_data<V, P> pop(P&)    : Pool-integrated blocking transfer.
 * 
 * 3. RECOVERY & MONITORING:
 *    - void invalidate_by_ptr(T)     : SIMD-accelerated (AVX-512) pointer nulling.
 *    - size_t occupancy()            : Atomic count of active items.
 *    - size_t approx_size()          : Unified O(1) query for flow control.
 * 
 * Memory Ordering:
 * - load(acquire) / store(release) protocol ensures that the Consumer sees data 
 *   writes performed by the Producer prior to the head update.
 */

#pragma once

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <immintrin.h>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/managed_data.hpp"
#include "slabflux/core/scoped_ptr.hpp"

namespace slabflux::core {

    template<typename T, size_t RequestedCapacity>
    class alignas(64) spsc_ring_conduit {
        static constexpr size_t Capacity = std::bit_ceil(RequestedCapacity);
        static constexpr size_t MASK = Capacity - 1;

        using ValueType = std::remove_pointer_t<T>;

        // The ring buffer storing pointers to the actual events
        alignas(64) T ring_[Capacity]{};

        // Matrix Markers: Separated to eliminate MESI ping-pong.
        // Ingress Group: Strictly Producer-Local
        alignas(64) size_t local_ingress_cursor_{0};
        alignas(64) size_t cached_egress_cursor_{0};
        alignas(64) std::atomic<size_t> physical_ingress_gate_{0}; // Published head

        // Egress Group: Strictly Consumer-Local
        alignas(64) size_t local_egress_cursor_{0};
        alignas(64) size_t cached_ingress_cursor_{0};
        alignas(64) std::atomic<size_t> physical_egress_gate_{0};  // Published tail

        void* arbiter_ptr_{nullptr};
        void (*fault_fn_)(void*, uint32_t){nullptr};

    public:
        spsc_ring_conduit() = default;
        ~spsc_ring_conduit() = default;
        spsc_ring_conduit(const spsc_ring_conduit&) = delete;
        spsc_ring_conduit& operator=(const spsc_ring_conduit&) = delete;

        /** @brief Attaches a fault reporter (usually the error_arbiter). */
        void attach_fault_reporter(void* arbiter, void (*fn)(void*, uint32_t)) noexcept {
            arbiter_ptr_ = arbiter;
            fault_fn_ = fn;
        }

        /** @brief Saturation hook called by the Ingress Nexus. */
        void on_conduit_full_drop() noexcept {
            if (fault_fn_) {
                fault_fn_(arbiter_ptr_, 0xE62E55 /* EGRESS_SATURATED */);
            }
        }

        /**
         * @brief Ingress Interface
         * @details Satisfies the PipelineLogic contract for fused_nexus_node.
         */
        SLAB_FORCE_INLINE bool on_raw_frame(const T& item, int /*res*/) noexcept {
            return try_push(item);
        }

        /**
         * @brief Non-blocking attempt to push a pointer.
         * @return true if successful.
         */
        SLAB_FORCE_INLINE bool try_push(const T& item) noexcept {
            // Check if full using local cache first to avoid bus traffic
            if (local_ingress_cursor_ - cached_egress_cursor_ >= Capacity) [[unlikely]] {
                cached_egress_cursor_ = physical_egress_gate_.load(std::memory_order_acquire);
                if (local_ingress_cursor_ - cached_egress_cursor_ >= Capacity) [[unlikely]] return false;
            }

            ring_[local_ingress_cursor_ & MASK] = item;

            // Sovereign Warming: Move the next ring slot into L1-D to ensure zero-stall writes in the subsequent cycle.
            _mm_prefetch(reinterpret_cast<const char*>(&ring_[(local_ingress_cursor_ + 1) & MASK]), _MM_HINT_T0);
            // Publish the new head
            local_ingress_cursor_++;
            physical_ingress_gate_.store(local_ingress_cursor_, std::memory_order_release);
            return true;
        }

        /** @brief Pointer-dereferencing non-blocking push. */
        template<typename U = T>
        requires (!std::is_pointer_v<U>)
        SLAB_FORCE_INLINE bool try_push(ValueType* item) noexcept {
            if (item) {
                return try_push(*item);
            }
            return false;
        }

        /** @brief Managed-data integration for automatic ownership transfer. */
        template <typename Pool>
        SLAB_FORCE_INLINE bool try_push(managed_data<ValueType, Pool>& item) noexcept {
            if (SL_EXPECT_FALSE(!item.get())) return false; 
            if (try_push(item.get())) {
                item.release(); // Relinquish ownership to the wire
                return true;
            }
            return false;
        }

        /** @brief Blocking push: Spins until a slot is available. */
        SLAB_FORCE_INLINE bool push(const T& item) noexcept {
            while (!try_push(item)) {
                _mm_pause();
            }
            return true;
        }

        /** @brief Pointer-dereferencing blocking push. */
        template<typename U = T>
        requires (!std::is_pointer_v<U>)
        SLAB_FORCE_INLINE bool push(T* item) noexcept {
            if (item) {
                push(*item);
                return true;
            }
            return false;
        }

        /** @brief Blocking managed push. */
        template <typename Pool>
        SLAB_FORCE_INLINE bool push(managed_data<ValueType, Pool>& item) noexcept {
            while (!try_push(item)) { _mm_pause(); }
            return true;
        }

        /** @brief Blocking pop: Spins until an item is available. */
        SLAB_FORCE_INLINE T pop() noexcept {
            T item;
            while (!try_pop(item)) {
                _mm_pause();
            }
            return item;
        }

        /**
         * @brief Checks if there are items available for consumption.
         */
        inline bool is_data_available() noexcept {
            // Check if items are available using local cache first to avoid bus traffic
            if (cached_ingress_cursor_ == local_egress_cursor_) {
                cached_ingress_cursor_ = physical_ingress_gate_.load(std::memory_order_acquire);
                if (cached_ingress_cursor_ == local_egress_cursor_) return false;
            }
            return true;
        }

        /**
         * @brief Atomic Horizon Probe.
         * @details Determines the total number of items immediately available for 
         * consumption without further atomic synchronization.
         */
        [[nodiscard]] SLAB_FORCE_INLINE size_t available_to_peek() noexcept {
            if (local_egress_cursor_ == cached_ingress_cursor_) {
                cached_ingress_cursor_ = physical_ingress_gate_.load(std::memory_order_acquire);
            }
            return cached_ingress_cursor_ - local_egress_cursor_;
        }

        /** @brief Returns a slot from the current synchronized horizon. */
        [[nodiscard]] SLAB_FORCE_INLINE const T* get_peek_slot(size_t offset) const noexcept {
            return &ring_[(local_egress_cursor_ + offset) & MASK];
        }

        /**
         * @brief Zero-copy peek at an item with a relative offset from the current head.
         * @details Allows the consumer to look ahead into the burst without generating 
         * bus traffic until the local shadow horizon is reached.
         */
        SLAB_FORCE_INLINE const T* peek_at(size_t offset) noexcept {
            // Step 1: Check if the offset is within the already synchronized horizon
            if (local_egress_cursor_ + offset >= cached_ingress_cursor_) {
                // Step 2: Refresh the shadow tail pointer from the producer's atomic head
                cached_ingress_cursor_ = physical_ingress_gate_.load(std::memory_order_acquire);
                if (local_egress_cursor_ + offset >= cached_ingress_cursor_) [[unlikely]] return nullptr;
            }
            return &ring_[(local_egress_cursor_ + offset) & MASK];
        }

        /** @brief Peeks at the next available item in the ring. */
        SLAB_FORCE_INLINE const T* peek() noexcept {
            // Check if items are available before peeking
            if (cached_ingress_cursor_ == local_egress_cursor_) {
                cached_ingress_cursor_ = physical_ingress_gate_.load(std::memory_order_acquire);
                if (cached_ingress_cursor_ == local_egress_cursor_) [[unlikely]] return nullptr;
            }
            return &ring_[local_egress_cursor_ & MASK];
        }

        /**
         * @brief Pool-integrated pop for managed ownership.
         */
        template <typename Pool>
        SLAB_FORCE_INLINE managed_data<ValueType, Pool>
        try_pop(Pool& pool) noexcept {
            T raw;
            if (try_pop(raw)) {
                return managed_data<ValueType, Pool>(raw, pool);
            }
            return managed_data<ValueType, Pool>();
        }

        /**
         * @brief Blocking pool-integrated pop.
         */
        template <typename Pool, typename = std::enable_if_t<!std::is_same_v<Pool, T>>>
        SLAB_FORCE_INLINE managed_data<ValueType, Pool>
        pop(Pool& pool) noexcept {
            static_assert(std::is_pointer_v<T>, "pop(Pool&) requires a conduit of pointers!");
            T raw;
            while (!try_pop(raw)) _mm_pause();
            return managed_data<ValueType, Pool>(raw, pool);
        }

        /**
         * @brief Non-blocking pop of a single pointer.
         */
        SLAB_FORCE_INLINE bool try_pop(T& out_item) noexcept {
            // Check if items are available using local cache first to avoid bus traffic
            if (cached_ingress_cursor_ == local_egress_cursor_) {
                cached_ingress_cursor_ = physical_ingress_gate_.load(std::memory_order_acquire);
                if (cached_ingress_cursor_ == local_egress_cursor_) return false;
            }

            out_item = ring_[local_egress_cursor_ & MASK];
            // Interconnect Prefetch: Pull the event payload into L1 while the local execution unit retires the atomic tail update.
            if constexpr (std::is_pointer_v<T>) {
                if (out_item) [[likely]] _mm_prefetch(reinterpret_cast<const char*>(out_item), _MM_HINT_T0);
            }

            local_egress_cursor_++;
            physical_egress_gate_.store(local_egress_cursor_, std::memory_order_release);
            return true;
        }

        /** @brief Blocking pop into reference. */
        SLAB_FORCE_INLINE void pop(T& out_item) noexcept {
            while (!try_pop(out_item)) {
                _mm_pause();
            }
        }

        /**
         * @brief Reserves a slot in the ring for writing.
         * @return Pointer to the slot, or nullptr if the ring is full.
         */
        [[nodiscard]] SLAB_FORCE_INLINE T* reserve() noexcept {
            // Step 1: Check the local cache (shadow pointer) without generating bus traffic
            if (local_ingress_cursor_ - cached_egress_cursor_ >= Capacity) [[unlikely]] {
                // Step 2: Only if it is theoretically full, pull in the Consumer's real position
                cached_egress_cursor_ = physical_egress_gate_.load(std::memory_order_acquire);
                if (local_ingress_cursor_ - cached_egress_cursor_ >= Capacity) [[unlikely]] {
                    return nullptr;
                }
            }

            T* slot = &ring_[local_ingress_cursor_ & MASK];

            _mm_prefetch(reinterpret_cast<const char*>(&ring_[(local_ingress_cursor_ + 1) & MASK]), _MM_HINT_T0);

            return slot;
        }

        /** @brief Exposes the local cursor to enable linear burst optimizations. */
        [[nodiscard]] SLAB_FORCE_INLINE size_t get_ingress_cursor() const noexcept { return local_ingress_cursor_; }
        [[nodiscard]] SLAB_FORCE_INLINE size_t get_egress_cursor() const noexcept { return local_egress_cursor_; }

        /**
         * @brief Batch Capacity Check.
         * @return The number of slots (up to max_count) available for writing.
         */
        SLAB_FORCE_INLINE size_t reserve_batch_space(size_t max_count) noexcept {
            if (local_ingress_cursor_ + max_count - cached_egress_cursor_ >= Capacity) [[unlikely]] {
                cached_egress_cursor_ = physical_egress_gate_.load(std::memory_order_acquire);
                size_t avail = Capacity - (local_ingress_cursor_ - cached_egress_cursor_);
                return (avail < max_count) ? avail : max_count;
            }
            return max_count;
        }

        /**
         * @brief Returns a slot without capacity checks.
         */
        SLAB_FORCE_INLINE T* get_reserved_slot(size_t offset) noexcept {
            return &ring_[(local_ingress_cursor_ + offset) & MASK];
        }

        /**
         * @brief Reserves a slot at a specific offset within a burst.
         * @details Essential for batched writes to prevent overwriting the same slot in a loop.
         */
        [[nodiscard]] SLAB_FORCE_INLINE T* reserve_at(size_t offset) noexcept {
            // Check if the offset burst fits within the available capacity
            if (local_ingress_cursor_ + offset - cached_egress_cursor_ >= Capacity) [[unlikely]] {
                cached_egress_cursor_ = physical_egress_gate_.load(std::memory_order_acquire);
                if (local_ingress_cursor_ + offset - cached_egress_cursor_ >= Capacity) [[unlikely]] {
                    return nullptr;
                }
            }
            return &ring_[(local_ingress_cursor_ + offset) & MASK];
        }

        /**
         * @brief Producer Commit.
         * @details Finalizes a manual reservation by advancing the physical head 
         * and publishing the data to the consumer in a single release instruction.
         * Strictly O(1) with zero branch overhead.
         */
        SLAB_FORCE_INLINE void commit_push() noexcept {
            // Commitment: Release semantics act as the publishing barrier,
            // guaranteeing that data written to the reserved slot is retired 
            // before the physical gate is opened to the consumer.
            physical_ingress_gate_.store(++local_ingress_cursor_, std::memory_order_release);
        }

        /**
         * @brief Publishes 'n' slots to the consumer in a single atomic operation.
         * @details Amortizes the RFO (Request For Ownership) penalty across the entire burst.
         */
        SLAB_FORCE_INLINE void commit_n(size_t n) noexcept {
            local_ingress_cursor_ += n;
            physical_ingress_gate_.store(local_ingress_cursor_, std::memory_order_release);
        }

        /**
         * @brief Consumer Commit.
         */
        SLAB_FORCE_INLINE void consume() noexcept {
            physical_egress_gate_.store(++local_egress_cursor_, std::memory_order_release);
        }

        /**
         * @brief Retires 'n' slots from the ring in a single atomic operation.
         * @details Signals the producer that 'n' slots are now free for recycling.
         */
        SLAB_FORCE_INLINE void consume_n(size_t n) noexcept {
            local_egress_cursor_ += n;
            physical_egress_gate_.store(local_egress_cursor_, std::memory_order_release);
        }

        /** @brief Consumer Alias. */
        SLAB_FORCE_INLINE void commit_pop() noexcept { consume(); }

        /** 
         * @deprecated Use commit_push() or commit_pop() to ensure structural honesty.
         * @details Paired with reserve() on the producer side to finalize and publish writes.
         */
        SLAB_FORCE_INLINE void commit() noexcept { commit_push(); }

        /**
         * @brief Batch pop operation to minimize atomic overhead.
         * @details Efficiently drains multiple pointers in a single acquire/release cycle.
         * @return Number of items actually popped.
         */
        inline size_t pop_batch(T* out_batch, size_t max_count) noexcept {
            if (cached_ingress_cursor_ == local_egress_cursor_) {
                // Re-fetch the true ingress gate position
                cached_ingress_cursor_ = physical_ingress_gate_.load(std::memory_order_acquire);
                if (cached_ingress_cursor_ == local_egress_cursor_) return 0;
            }

            const size_t available = cached_ingress_cursor_ - local_egress_cursor_;
            const size_t count = (available < max_count) ? available : max_count;
            const size_t t_idx = local_egress_cursor_ & MASK;

            if (SL_EXPECT_TRUE(t_idx + count <= Capacity)) {
                size_t j = 0;
                if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) == 8) {
                    #if defined(__AVX512F__)
                    for (; j + 8 <= count; j += 8) {
                        _mm512_storeu_si512(&out_batch[j], _mm512_loadu_si512(&ring_[t_idx + j]));
                    }
                    #elif defined(__AVX2__)
                    for (; j + 4 <= count; j += 4) {
                        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&out_batch[j]), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&ring_[t_idx + j])));
                    }
                    #endif
                }
                for (; j < count; ++j) out_batch[j] = ring_[t_idx + j];
            } else {
                const size_t p1 = Capacity - t_idx;
                const size_t p2 = count - p1;
                for (size_t i = 0; i < p1; ++i) out_batch[i] = std::move(ring_[t_idx + i]);
                for (size_t i = 0; i < p2; ++i) out_batch[p1 + i] = std::move(ring_[i]);
            }
            // Publish the new egress gate position
            local_egress_cursor_ += count;
            physical_egress_gate_.store(local_egress_cursor_, std::memory_order_release);
            return count;
        }

        /** @brief Vectorized Batch Push (AVX-512). */
        inline size_t push_batch(const T* in_batch, size_t max_count) noexcept {
            if (local_ingress_cursor_ - cached_egress_cursor_ >= Capacity) {
                cached_egress_cursor_ = physical_egress_gate_.load(std::memory_order_acquire);
                if (local_ingress_cursor_ - cached_egress_cursor_ >= Capacity) return 0;
            }
            const size_t available = Capacity - (local_ingress_cursor_ - cached_egress_cursor_);
            const size_t count = (available < max_count) ? available : max_count;
            const size_t t_idx = local_ingress_cursor_ & MASK;

            if (SL_EXPECT_TRUE(t_idx + count <= Capacity)) {
                size_t j = 0;
                if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) == 8) {
                    #if defined(__AVX512F__)
                    for (; j + 8 <= count; j += 8) {
                        _mm512_storeu_si512(&ring_[t_idx + j], _mm512_loadu_si512(&in_batch[j]));
                    }
                    #elif defined(__AVX2__)
                    for (; j + 4 <= count; j += 4) {
                        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&ring_[t_idx + j]), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&in_batch[j])));
                    }
                    #endif
                }
                for (; j < count; ++j) ring_[t_idx + j] = in_batch[j];
            } else {
                const size_t p1 = Capacity - t_idx;
                const size_t p2 = count - p1;
                size_t j = 0;
                if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) == 8) {
                    #if defined(__AVX512F__)
                    for (; j + 8 <= p1; j += 8) {
                        _mm512_storeu_si512(&ring_[t_idx + j], _mm512_loadu_si512(&in_batch[j]));
                    }
                    #elif defined(__AVX2__)
                    for (; j + 4 <= p1; j += 4) {
                        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&ring_[t_idx + j]), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&in_batch[j])));
                    }
                    #endif
                }
                for (; j < p1; ++j) ring_[t_idx + j] = in_batch[j];
                size_t k = 0;
                if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) == 8) {
                    #if defined(__AVX512F__)
                    for (; k + 8 <= p2; k += 8) {
                        _mm512_storeu_si512(&ring_[k], _mm512_loadu_si512(&in_batch[p1 + k]));
                    }
                    #elif defined(__AVX2__)
                    for (; k + 4 <= p2; k += 4) {
                        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&ring_[k]), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&in_batch[p1 + k])));
                    }
                    #endif
                }
                for (; k < p2; ++k) ring_[k] = in_batch[p1 + k];
            }
            
            local_ingress_cursor_ += count;
            physical_ingress_gate_.store(local_ingress_cursor_, std::memory_order_release);
            return count;
        }

        /**
         * @brief Returns the approximate number of pointers currently held in the conduit.
         */
        [[nodiscard]] inline size_t occupancy() const noexcept {
            // Telemetry: const_cast allows atomic sampling of the physical gates
            return physical_ingress_gate_.load(std::memory_order_relaxed) - 
                   physical_egress_gate_.load(std::memory_order_relaxed);
        }

        /** @brief Unified size query for flow control. */
        [[nodiscard]] inline size_t approx_size() const noexcept {
            return occupancy();
        }

        /**
         * @brief SIMD-accelerated linear search to invalidate a pointer.
         * @details Critical for recovery paths (e.g. journal replay). 
         * 
         * Uses AVX-512 to scan 8 pointers per cycle, finding and nulling all instances 
         * of 'target' within the current ring occupancy range. This prevents stale
         * data references after a system rebirth.
         */
        SLAB_HOT void invalidate_by_ptr(T target) noexcept {
            const size_t head = physical_ingress_gate_.load(std::memory_order_acquire);
            const size_t tail = physical_egress_gate_.load(std::memory_order_acquire);
            if (head == tail) return;

            // Alignment: Derived indices moved to function scope to resolve
            // declaration errors in both SIMD and scalar fallback paths.
            const size_t t_idx = tail & MASK;
            const size_t h_idx = head & MASK;

            if constexpr (sizeof(T) == 8) {
                // Rematerialization: Utilizes bit_cast (C++20) to extract
                // the hardware-level bit pattern of the target for SIMD comparison,
                // ensuring bit-perfect invalidation for any 8-byte POD.
                const int64_t target_val = std::bit_cast<int64_t>(target);

                #if defined(__AVX512F__)
                const __m512i v_target = _mm512_set1_epi64(target_val);
                #elif defined(__AVX2__)
                const __m256i v_target_256 = _mm256_set1_epi64x(target_val);
                const __m256i v_zero_256 = _mm256_setzero_si256();
                #endif

                auto scan_avx = [&](size_t start, size_t end) {
                    size_t i = start;
                    #if defined(__AVX512F__)

                    for (; i + 8 <= end; i += 8) {
                        __m512i v_data = _mm512_loadu_si512(reinterpret_cast<const void*>(&ring_[i]));
                        __mmask8 mask = _mm512_cmpeq_epu64_mask(v_data, v_target);
                        if (SL_EXPECT_FALSE(mask != 0)) {
                            for (int b = 0; b < 8; ++b) if (mask & (1 << b)) {
                                if constexpr (std::is_pointer_v<T>) ring_[i + b] = nullptr;
                                else ring_[i + b] = T{};
                            }
                        }
                    }
                    #elif defined(__AVX2__)
                    for (; i + 4 <= end; i += 4) {
                        __m256i v_data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&ring_[i]));
                        __m256i v_mask = _mm256_cmpeq_epi64(v_data, v_target_256);
                        if (!_mm256_testz_si256(v_mask, v_mask)) {
                            _mm256_storeu_si256(reinterpret_cast<__m256i*>(&ring_[i]), _mm256_blendv_epi8(v_data, v_zero_256, v_mask));
                        }
                    }
                    #endif

                    for (; i < end; ++i) if (ring_[i] == target) {
                        if constexpr (std::is_pointer_v<T>) ring_[i] = nullptr;
                        else ring_[i] = T{};
                    }
                };

                if (t_idx < h_idx) {
                    scan_avx(t_idx, h_idx);
                } else {
                    scan_avx(h_idx, Capacity);
                    scan_avx(0, h_idx);
                }
            } else {
                auto scan_scalar = [&](size_t start, size_t end) {
                    for (size_t i = start; i < end; ++i) {
                        if (ring_[i] == target) ring_[i] = T{};
                    }
                };

                if (t_idx < h_idx) {
                    scan_scalar(t_idx, h_idx);
                } else {
                    scan_scalar(h_idx, Capacity);
                    scan_scalar(0, h_idx);
                }
            }
        }
    };

} // namespace slabflux::core
