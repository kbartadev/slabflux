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
#include <immintrin.h>
#include <utility>
#include <cstring>
#include <cstddef>
#include <array>
#include <bit>
#include <algorithm>
#include "slabflux/core/alignment_checks.hpp"
#include "slabflux/core/wire_frame_lsn.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/managed_data.hpp"
#include "slabflux/core/scoped_ptr.hpp"

namespace slabflux::core {

    template <typename T>
    concept ConduitElement = requires {
        requires std::is_trivially_copyable_v<T>;
    };

    /**
     * @brief Fast POD-only SPSC conduit.
     * @details A high-performance ring buffer optimized for Trivial/POD types.
     * * API Contract:
     * 1. Blocking Operations (push/pop):
     * Strictly spinning. Either succeeds or triggers handle_critical_error if 
     * a NULL sentinel is encountered in pointer modes.
     * 2. Non-Blocking Operations (try_push/try_pop):
     * Returns bool status immediately. try_pop(T&) unambiguously distinguishes 
     * between an empty conduit (false) and a valid nullptr item (true).
     * * ========================================================================
     * API REFERENCE: spsc_conduit<T, Capacity>
     * ========================================================================
     * 1. INGRESS (PRODUCER PATH):
     * - bool try_push(const T& item)  : Non-blocking push. Returns false if full.
     * - bool try_push(T* item)        : Pointer-dereferencing non-blocking push.
     * - void push(const T& item)      : Blocking push. Spins until slot available.
     * - void push(T* item)            : Pointer-dereferencing blocking push.
     * - void push(managed_data<T, P>&): Ownership-integrated push (dereferences).
     * * 2. EGRESS (CONSUMER PATH):
     * - bool try_pop(T& out)          : Non-blocking pop into reference.
     * - T pop()                       : Blocking pop (returns value).
     * - void pop(T& out)              : Blocking pop into reference.
     * - size_t pop_batch(T*, size_t)  : Vectorized multi-pointer drain (AVX-512/AVX2).
     * - T* peek()                     : Non-blocking lookahead (returns ptr to slot).
     * - void consume()                : Advances head after a successful peek.
     * - managed_data<V, P> try_pop(P&): Pool-integrated non-blocking pop.
     * - managed_data<V, P> pop(P&)    : Pool-integrated blocking pop.
     * * 3. RECOVERY & MONITORING:
     * - void invalidate_by_ptr(T)     : SIMD-accelerated (AVX-512) pointer nulling.
     * - size_t occupancy()            : Atomic count of items currently in the ring.
     * - size_t approx_size()          : Unified O(1) size query for flow control.
     */
    // Fixed constant replaces std::hardware_destructive_interference_size to ensure 
    // ABI stability and silence -Winterference-size warnings on GCC.
    static constexpr size_t SLAB_L1_CACHE_LINE = 64;

    template <ConduitElement T, std::size_t RequestedSize>
    class alignas(SLAB_L1_CACHE_LINE) spsc_conduit {
    public:
        using value_type = T;
        using value_type_pod = std::remove_pointer_t<T>;
        
        static constexpr std::size_t Size = std::bit_ceil(std::max<size_t>(RequestedSize, 2));
        static constexpr std::size_t Mask = Size - 1;

    private:
        // ====================================================================
        // MEMORY TOPOLOGY
        // ====================================================================
        alignas(SLAB_L1_CACHE_LINE) T buffer_[Size]{};
        
        // --------------------------------------------------------------------
        // PRODUCER STATE: Grouped to 64 bytes to eliminate internal RFO stalls.
        // --------------------------------------------------------------------
        alignas(SLAB_L1_CACHE_LINE) std::size_t local_ingress_cursor_{0};
        std::atomic<std::size_t> physical_ingress_gate_{0}; // Published Tail (Shared)
        std::size_t cached_egress_cursor_{0};               // Shadow Head (Producer local view)

        // --------------------------------------------------------------------
        // CONSUMER STATE: Grouped to 64 bytes to eliminate internal RFO stalls.
        // --------------------------------------------------------------------
        alignas(SLAB_L1_CACHE_LINE) std::size_t local_egress_cursor_{0};
        std::atomic<std::size_t> physical_egress_gate_{0};  // Published Head (Shared)
        std::size_t cached_ingress_cursor_{0};              // Shadow Tail (Consumer local view)

    public:
        spsc_conduit() noexcept = default;

    private:
        void* arbiter_ptr_{nullptr};
        void (*fault_fn_)(void*, uint32_t){nullptr};

    public:
        /**
         * @brief Ingress Interface
         * @details Satisfies the PipelineLogic contract for fused_nexus_node.
         * @param item Pointer to the frame data.
         */
        SLAB_FORCE_INLINE bool on_raw_frame(const T& item, int /*res*/) noexcept {
            return try_push(item);
        }

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

        /** @brief Non-blocking push. */
        SLAB_FORCE_INLINE bool try_push(const T& item) noexcept {
            if (local_ingress_cursor_ - cached_egress_cursor_ >= Size) [[unlikely]] {
                cached_egress_cursor_ = physical_egress_gate_.load(std::memory_order_acquire);
                if (local_ingress_cursor_ - cached_egress_cursor_ >= Size) [[unlikely]] return false;
            }

            buffer_[local_ingress_cursor_ & Mask] = item;
            
            _mm_prefetch(reinterpret_cast<const char*>(&buffer_[(local_ingress_cursor_ + 1) & Mask]), _MM_HINT_T0);
            
            local_ingress_cursor_++;
            physical_ingress_gate_.store(local_ingress_cursor_, std::memory_order_release);
            return true;
        }

        /** @brief Pointer-dereferencing non-blocking push. */
        template<typename U = T>
        requires (!std::is_pointer_v<U>)
        SLAB_FORCE_INLINE bool try_push(T* item) noexcept {
            if (SL_EXPECT_FALSE(!item)) return false;
            return try_push(*item);
        }

        /** @brief Managed-data integration. */
        template <typename Pool>
        SLAB_FORCE_INLINE bool try_push(managed_data<value_type_pod, Pool>& item) noexcept {
            if constexpr (std::is_pointer_v<T>) {
                T ptr = item.get();
                if (try_push(ptr)) {
                    item.release();
                    return true;
                }
            } else {
                if (try_push(*item)) {
                    return true;
                }
            }
            return false;
        }

        /** @brief Non-blocking attempt to pop an item. */
        SLAB_FORCE_INLINE bool try_pop(T& out_item) noexcept {
            if (local_egress_cursor_ == cached_ingress_cursor_) {
                cached_ingress_cursor_ = physical_ingress_gate_.load(std::memory_order_acquire);
                if (local_egress_cursor_ == cached_ingress_cursor_) [[unlikely]] return false;
            }

            out_item = buffer_[local_egress_cursor_ & Mask];
            _mm_prefetch(reinterpret_cast<const char*>(&buffer_[(local_egress_cursor_ + 1) & Mask]), _MM_HINT_T0);

            if constexpr (std::is_pointer_v<T>) {
                if (out_item) [[likely]] _mm_prefetch(reinterpret_cast<const char*>(out_item), _MM_HINT_T0);
            }

            local_egress_cursor_++;
            physical_egress_gate_.store(local_egress_cursor_, std::memory_order_release);
            return true;
        }

        /**
         * @brief Managed-data integration for conduits of pointers.
         * @return A managed_data handle if successful, or an empty handle if the conduit is empty.
         */
        template <typename Pool, typename = std::enable_if_t<!std::is_same_v<Pool, T>>>
        SLAB_FORCE_INLINE managed_data<value_type_pod, Pool>
        try_pop(Pool& pool) noexcept {
            static_assert(std::is_pointer_v<T>, "try_pop(Pool&) requires a conduit of pointers!");
            T raw;
            if (try_pop(raw)) {
                return managed_data<value_type_pod, Pool>(raw, pool);
            }
            return managed_data<value_type_pod, Pool>();
        }

        /** @brief Returns a pointer to the head of the queue without advancing. */
        SLAB_FORCE_INLINE T* peek() noexcept {
            if (local_egress_cursor_ == cached_ingress_cursor_) {
                cached_ingress_cursor_ = physical_ingress_gate_.load(std::memory_order_acquire);
                if (local_egress_cursor_ == cached_ingress_cursor_) [[unlikely]] return nullptr;
            }
            return &buffer_[local_egress_cursor_ & Mask];
        }

        /** @brief Advances the head, effectively consuming the item peeked at. */
        SLAB_FORCE_INLINE void consume() noexcept {
            physical_egress_gate_.store(++local_egress_cursor_, std::memory_order_release);
        }

        /** @brief Blocking push using CPU pause to prevent pipeline stalls. */
        SLAB_FORCE_INLINE bool push(const T& item) noexcept {
            while (!try_push(item)) _mm_pause();
            return true;
        }

        /** @brief Blocking pointer-based push. */
        template<typename U = T>
        requires (!std::is_pointer_v<U>)
        SLAB_FORCE_INLINE bool push(T* item) noexcept {
            if (SL_EXPECT_FALSE(!item)) return false;
            while (!try_push(item)) _mm_pause();
            return true;
        }

        /** @brief Blocking pop into reference. */
        SLAB_FORCE_INLINE void pop(T& out_item) noexcept {
            while (!try_pop(out_item)) {
                _mm_pause();
            }
        }

        /** @brief Blocking pop returning value. */
        SLAB_FORCE_INLINE T pop() noexcept {
            T item;
            while (!try_pop(item)) {
                _mm_pause();
            }
            return item;
        }

        /** @brief Blocking managed pop for conduits of pointers. */
        template <typename Pool, typename = std::enable_if_t<!std::is_same_v<Pool, T>>>
        SLAB_FORCE_INLINE managed_data<value_type_pod, Pool>
        pop(Pool& pool) noexcept {
            static_assert(std::is_pointer_v<T>, "pop(Pool&) requires a conduit of pointers!");
            T raw = pop();
            return managed_data<value_type_pod, Pool>(raw, pool);
        }

        /** @brief Managed-data integration. */
        template <typename Pool>
        SLAB_FORCE_INLINE void push(managed_data<value_type_pod, Pool>& managed_item) noexcept {
            while (SL_EXPECT_FALSE(!try_push(managed_item))) _mm_pause();
        }

        /**
         * @brief Batch pop operation to minimize atomic overhead.
         * @details Efficiently drains multiple items in a single acquire/release cycle.
         * @return Number of items actually popped.
         */
        inline size_t pop_batch(T* out_batch, size_t max_count) noexcept {
            if (local_egress_cursor_ == cached_ingress_cursor_) {
                cached_ingress_cursor_ = physical_ingress_gate_.load(std::memory_order_acquire);
                if (local_egress_cursor_ == cached_ingress_cursor_) return 0;
            }

            size_t available = cached_ingress_cursor_ - local_egress_cursor_;
            size_t count = (available < max_count) ? available : max_count;
            const size_t t_idx = local_egress_cursor_ & Mask;

            auto move_range = [&](size_t start_idx, size_t n, size_t out_offset) {
                size_t j = 0;
                if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) == 8) {
                    #if defined(__AVX512F__)
                    for (; j + 8 <= n; j += 8) {
                        _mm512_storeu_si512(&out_batch[out_offset + j], _mm512_loadu_si512(&buffer_[start_idx + j]));
                    }
                    #elif defined(__AVX2__)
                    for (; j + 4 <= n; j += 4) {
                        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&out_batch[out_offset + j]), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&buffer_[start_idx + j])));
                    }
                    #endif
                }
                for (; j < n; ++j) out_batch[out_offset + j] = std::move(buffer_[start_idx + j]);
            };

            if (SL_EXPECT_TRUE(t_idx + count <= Size)) {
                move_range(t_idx, count, 0);
            } else {
                const size_t first_part = Size - t_idx;
                move_range(t_idx, first_part, 0);
                move_range(0, count - first_part, first_part);
            }

            local_egress_cursor_ += count;
            physical_egress_gate_.store(local_egress_cursor_, std::memory_order_release);
            return count;
        }

        /**
         * @brief Vectorized Batch Push (AVX-512).
         * @details Efficiently fills multiple slots in a single release cycle.
         * @return Number of items successfully pushed.
         */
        inline size_t push_batch(const T* in_batch, size_t max_count) noexcept {
            if (local_ingress_cursor_ - cached_egress_cursor_ >= Size) {
                cached_egress_cursor_ = physical_egress_gate_.load(std::memory_order_acquire);
                if (local_ingress_cursor_ - cached_egress_cursor_ >= Size) [[unlikely]] return 0;
            }

            const size_t available = Size - (local_ingress_cursor_ - cached_egress_cursor_);
            const size_t count = (available < max_count) ? available : max_count;
            const size_t t_idx = local_ingress_cursor_ & Mask;

            auto fill_range = [&](size_t start, size_t n, size_t in_off) {
                size_t j = 0;
                if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) == 8) {
                    #if defined(__AVX512F__)
                    for (; j + 8 <= n; j += 8) {
                        _mm512_storeu_si512(&buffer_[start + j], _mm512_loadu_si512(&in_batch[in_off + j]));
                    }
                    #elif defined(__AVX2__)
                    for (; j + 4 <= n; j += 4) {
                        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&buffer_[start + j]), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&in_batch[in_off + j])));
                    }
                    #endif
                }
                for (; j < n; ++j) buffer_[start + j] = in_batch[in_off + j];
            };

            if (SL_EXPECT_TRUE(t_idx + count <= Size)) {
                fill_range(t_idx, count, 0);
            } else {
                const size_t first_part = Size - t_idx;
                fill_range(t_idx, first_part, 0);
                fill_range(0, count - first_part, first_part);
            }
            
            local_ingress_cursor_ += count;
            physical_ingress_gate_.store(local_ingress_cursor_, std::memory_order_release);
            return count;
        }

        /**
         * @brief Rewinds the head marker to "un-consume" failed items.
         * @details Critical for hardware backpressure handling in io_uring/XDP.
         */
        inline void revert_batch(const T* batch, size_t count) noexcept {
            if (count == 0) return;
            
            for (size_t i = 0; i < count; ++i) {
                local_egress_cursor_--;
                buffer_[local_egress_cursor_ & Mask] = std::move(batch[count - 1 - i]);
            }
            physical_egress_gate_.store(local_egress_cursor_, std::memory_order_release);
        }

        /**
         * @brief SIMD-accelerated linear search to invalidate an item.
         */
        SLAB_HOT void invalidate_by_ptr(T target) noexcept {
            const size_t h = physical_ingress_gate_.load(std::memory_order_acquire);
            const size_t t = physical_egress_gate_.load(std::memory_order_acquire);
            if (h == t) return;

            const size_t h_idx = h & Mask;
            const size_t t_idx = t & Mask;

            if constexpr (sizeof(T) == 8) {
                // Utilizes bit_cast (C++20) to extract
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
                        __m512i v_data = _mm512_loadu_si512(reinterpret_cast<const void*>(&buffer_[i]));
                        __mmask8 mask = _mm512_cmpeq_epi64_mask(v_data, v_target);
                        if (SL_EXPECT_FALSE(mask != 0)) {
                            for (int b = 0; b < 8; ++b) if (mask & (1 << b)) {
                                buffer_[i + b] = T{};
                            }
                        }
                    }
                    #elif defined(__AVX2__)
                    for (; i + 4 <= end; i += 4) {
                        __m256i v_data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&buffer_[i]));
                        __m256i v_mask = _mm256_cmpeq_epi64(v_data, v_target_256);
                        if (!_mm256_testz_si256(v_mask, v_mask)) {
                            _mm256_storeu_si256(reinterpret_cast<__m256i*>(&buffer_[i]), _mm256_blendv_epi8(v_data, v_zero_256, v_mask));
                        }
                    }
                    #endif

                    for (; i < end; ++i) if (buffer_[i] == target) {
                        buffer_[i] = T{};
                    }
                };

                if (h_idx < t_idx) {
                    scan_avx(h_idx, t_idx);
                } else {
                    scan_avx(t_idx, Size);
                    scan_avx(0, t_idx);
                }
            } else {
                auto scan_scalar = [&](size_t start, size_t end) {
                    for (size_t i = start; i < end; ++i) {
                        if (buffer_[i] == target) buffer_[i] = T{};
                    }
                };

                if (h_idx < t_idx) {
                    scan_scalar(h_idx, t_idx);
                } else {
                    scan_scalar(t_idx, Size);
                    scan_scalar(0, t_idx);
                }
            }
        }

        /** @brief Unified size query for flow control. */
        [[nodiscard]] inline size_t occupancy() const noexcept {
            return physical_ingress_gate_.load(std::memory_order_relaxed) - 
                   physical_egress_gate_.load(std::memory_order_relaxed);
        }

        /** @brief Unified size query for flow control. */
        [[nodiscard]] inline size_t approx_size() const noexcept {
            return occupancy();
        }
    };
}
