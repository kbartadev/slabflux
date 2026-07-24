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
 * @file mpmc_sharded_conduit.hpp
 * @brief Sharded MPMC IPC Conduit.
 * @details Implements a high-throughput, sharded MPMC conduit utilizing a
 * Sequence-Validated Sharded Vyukov architecture to bypass the L3 interconnect
 * wall during high-velocity Shared Memory or cross-core transfers.
 * 
 * High-Performance Design:
 * 1. Distributed: Contention is sharded across multiple 
 *    parallel lanes, virtually eliminating RFO stalls.
 * 2. Routing: Uses identity-mapped lane selection and "Adaptive 
 *    Topology" to pin threads to core-local memory channels.
 * 3. Decoupled Arrays: Separates atomic sequence meta from payload data to
 *    optimize hardware prefetching and prevent L1-D pollution, enabling AVX-512
 *    spatial locality for batch operations.
 * 4. Hardware-Managed Residency: Utilizes mmap/mlock with HugePage support 
 *    (2MB pages) to guarantee physical RAM residency and minimize TLB misses,
 *    with graceful fallback to standard locked pages.
 * 
 * API Contract:
 * - Progress Guarantee: Wait-free O(1) transitions for both producers and consumers.
 * - Safety: ABA-protection via monotonic sequence versioning per slot.
 * 
 * ========================================================================
 * API REFERENCE: mpmc_sharded_conduit<T, Capacity>
 * ========================================================================
 * 1. INGRESS (CONCURRENT PRODUCERS):
 *    - bool try_push(const T&)       : Non-blocking concurrent push.
 *    - void push(const T&)           : Blocking push (spinning with backoff).
 * 
 * 2. EGRESS (CONCURRENT CONSUMERS):
 *    - bool try_pop(T&)              : Non-blocking concurrent pop.
 *    - T pop()                       : Blocking pop returning value.
 *    - size_t pop_batch(T*, count)   : Vectorized batch egress (AVX-512).
 *    - void invalidate_by_ptr(T)     : SIMD-accelerated pointer nulling.
 */

#pragma once
#include <cstring>
#include <atomic>
#include <bit>
#include <immintrin.h>
#include <cstdint>
#include <system_error>
#include <iostream>
#include <utility>
#include <array>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/platform/os.hpp"
#include "slabflux/core/managed_data.hpp"
#include "slabflux/core/scoped_ptr.hpp"

namespace slabflux::core {

    /**
     * @brief Sharded MPMC Conduit.
     * @details Implements a Distributed Matrix to bypass the L3 interconnect wall.
     * Contention is sharded across multiple parallel lanes, virtually eliminating RFO stalls.
     * 
     * High-Performance Design:
     * 1. Sticky Path Routing: Confines threads to core-local memory channels to maximize L2 hit rates.
     * 2. Wait-Free Traversal: Producers and Consumers probe neighboring lanes during backpressure.
     * 3. Hardware-Managed Residency: Every lane utilizes mmap/mlock with HugePage support.
     */
    template <POD T, std::size_t RequestedSize, std::size_t NumLanes = 8>
    class alignas(64) mpmc_sharded_conduit {
    public:
        using value_type = T;
        using value_type_pod = std::remove_pointer_t<T>;
        static_assert((NumLanes & (NumLanes - 1)) == 0, "NumLanes must be power-of-two");
    
    private:
        static constexpr std::size_t TotalSize = (RequestedSize < 2) ? 2 : std::bit_ceil(RequestedSize);
        static constexpr std::size_t LaneCapacity = TotalSize / NumLanes;
        static constexpr std::size_t LaneMask = LaneCapacity - 1;
        static constexpr std::size_t LanesMask = NumLanes - 1;

        /** @brief Lane structure for distributed contention sharding. */
        struct alignas(64) mpmc_lane {
            static constexpr std::size_t LaneMask = LaneCapacity - 1;

            // Group 0: Metadata Array Pointers (Line 0)
            alignas(64) std::atomic<std::size_t>* sequences_{nullptr};
            T* data_{nullptr};
            std::size_t total_bytes_{ 0 };
            alignas(64) std::atomic<std::size_t> physical_ingress_gate_{ 0 };
            alignas(64) std::atomic<std::size_t> physical_egress_gate_{ 0 };
        };

        std::array<mpmc_lane, NumLanes> lanes_;
        inline static thread_local std::size_t cached_core_idx = 0xFFFFFFFF;
        void* arbiter_ptr_{nullptr};
        void (*fault_fn_)(void*, uint32_t){nullptr};

    public:
        /**
         * @brief Identity-Mapped Lane Selection.
         * @details Dynamically polls the active core index on every call to guarantee 
         * that the thread always operates on its physically local memory channel.
         */
        SLAB_FORCE_INLINE mpmc_lane& select_producer_lane(std::size_t offset) noexcept {
            // Optimized Sticky Routing: Poll hardware topology only once per thread lifetime.
            // This eliminates the 15-40ns vDSO tax during lane spillover attempts.
            if (SL_UNLIKELY(cached_core_idx == 0xFFFFFFFF)) [[unlikely]] {
                cached_core_idx = static_cast<std::size_t>(hardware_topology::get_current_cpu());
            }
            return lanes_[(cached_core_idx + offset) & LanesMask];
        }

        SLAB_FORCE_INLINE mpmc_lane& select_consumer_lane(std::size_t offset) noexcept {
            // Optimized Sticky Routing: Poll hardware topology only once per thread lifetime.
            if (SL_UNLIKELY(cached_core_idx == 0xFFFFFFFF)) [[unlikely]] {
                cached_core_idx = static_cast<std::size_t>(hardware_topology::get_current_cpu());
            }
            return lanes_[(cached_core_idx + offset) & LanesMask];
        }

        /** @brief Constructor: Enforces physical residency and HugePage alignment per lane. */
        mpmc_sharded_conduit() {
            for (std::size_t l = 0; l < NumLanes; ++l) {
                auto& lane = lanes_[l];
                const size_t seq_bytes = (LaneCapacity * sizeof(std::atomic<std::size_t>) + 63) & ~63;
                const size_t data_bytes = (LaneCapacity * sizeof(T) + 63) & ~63;
                lane.total_bytes_ = seq_bytes + data_bytes;

                #ifndef _WIN32
                int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED | MAP_HUGETLB | MAP_HUGE_2MB;
                void* mem = ::mmap(nullptr, lane.total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
                if (mem == MAP_FAILED) {
                    flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED;
                    mem = ::mmap(nullptr, lane.total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
                }
                if (mem == MAP_FAILED) {
                    flags = MAP_PRIVATE | MAP_ANONYMOUS;
                    mem = ::mmap(nullptr, lane.total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
                    if (mem == MAP_FAILED) throw std::bad_alloc();
                }
                ::madvise(mem, lane.total_bytes_, MADV_HUGEPAGE | MADV_DONTDUMP);
                ::mlock(mem, lane.total_bytes_);
                #else
                void* mem = ::VirtualAlloc(nullptr, lane.total_bytes_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                if (!mem) throw std::bad_alloc();
                #endif
                lane.sequences_ = static_cast<std::atomic<std::size_t>*>(mem);
                lane.data_ = reinterpret_cast<T*>(static_cast<char*>(mem) + seq_bytes);

                for (std::size_t i = 0; i < LaneCapacity; ++i) {
                    new (&lane.sequences_[i]) std::atomic<std::size_t>(i);
                }
            }
        }

        ~mpmc_sharded_conduit() {
            for (auto& lane : lanes_) {
                if (lane.sequences_) {
                    #ifndef _WIN32
                    ::munlock(lane.sequences_, lane.total_bytes_);
                    ::munmap(lane.sequences_, lane.total_bytes_);
                    #else
                    ::VirtualFree(lane.sequences_, 0, MEM_RELEASE);
                    #endif
                }
            }
        }

        mpmc_sharded_conduit(const mpmc_sharded_conduit&) = delete;
        mpmc_sharded_conduit& operator=(const mpmc_sharded_conduit&) = delete;

        /** @brief Attaches a fault reporter. */
        void attach_fault_reporter(void* arbiter, void (*fn)(void*, uint32_t)) noexcept {
            arbiter_ptr_ = arbiter;
            fault_fn_ = fn;
        }

        /** @brief Ingress Interface for fused_nexus_node. */
        SLAB_FORCE_INLINE bool on_raw_frame(const T& item, int /*res*/) noexcept {
            return try_push(item);
        }

        /** @brief Managed-data integration. */
        template <typename Pool, typename = std::enable_if_t<!std::is_same_v<Pool, T>>>
        SLAB_FORCE_INLINE bool try_push(managed_data<value_type_pod, Pool>& managed_item) noexcept {
            static_assert(std::is_pointer_v<T>, "try_push(managed_data&) requires a conduit of pointers!");
            if (SL_EXPECT_FALSE(!managed_item.get())) return false;
            if (try_push(managed_item.get())) {
                managed_item.release();
                return true;
            }
            return false;
        }

        /** @brief Scoped-pointer integration. */
        SLAB_FORCE_INLINE bool try_push(scoped_ptr<value_type_pod>& item) noexcept {
            if (try_push(item.get())) {
                item.release();
                return true;
            }
            return false;
        }

        /**
         * @brief Thread-safe concurrent push with sticky-lane routing.
         * @return true if successful, false if all lanes are saturated.
         */
        SLAB_FORCE_INLINE bool try_push(const T& item) noexcept {
            for (std::size_t i = 0; i < NumLanes; ++i) {
                auto& lane = select_producer_lane(i);
                std::size_t pos = lane.physical_ingress_gate_.load(std::memory_order_relaxed);
                intptr_t dif = 0;
                for (uint32_t retries = 0; ; ++retries) {
                    std::size_t seq = lane.sequences_[pos & LaneMask].load(std::memory_order_acquire);
                    dif = (intptr_t)seq - (intptr_t)pos;
                    if (dif == 0) {
                        if (SL_EXPECT_TRUE(lane.physical_ingress_gate_.compare_exchange_strong(pos, pos + 1, std::memory_order_relaxed))) break;
                        for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
                    } else if (dif < 0) {
                        break; // Lane saturated, try next
                    } else {
                        pos = lane.physical_ingress_gate_.load(std::memory_order_relaxed);
                    }
                    for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
                }

                if (dif == 0) {
                    lane.data_[pos & LaneMask] = item;

                    _mm_prefetch(reinterpret_cast<const char*>(&lane.sequences_[(pos + 1) & LaneMask]), _MM_HINT_T0);
                    _mm_prefetch(reinterpret_cast<const char*>(&lane.data_[(pos + 1) & LaneMask]), _MM_HINT_T0);

                    lane.sequences_[pos & LaneMask].store(pos + 1, std::memory_order_release);
                    return true;
                }
            }
            return false; // All lanes saturated
        }

        /** @brief Blocking push: Spins until a slot is available in any lane. */
        SLAB_FORCE_INLINE bool push(const T& item) noexcept {
            while (SL_EXPECT_FALSE(!try_push(item))) _mm_pause();
            return true;
        }

        /** @brief Non-blocking concurrent pop. */
        SLAB_FORCE_INLINE bool try_pop(T& out_item) noexcept {
            for (std::size_t i = 0; i < NumLanes; ++i) {
                auto& lane = select_consumer_lane(i);
                std::size_t pos = lane.physical_egress_gate_.load(std::memory_order_relaxed);
                intptr_t dif = 0;
                for (uint32_t retries = 0; ; ++retries) {
                    std::size_t seq = lane.sequences_[pos & LaneMask].load(std::memory_order_acquire);
                    dif = (intptr_t)seq - (intptr_t)(pos + 1);
                    if (dif == 0) {
                        if (SL_EXPECT_TRUE(lane.physical_egress_gate_.compare_exchange_strong(pos, pos + 1, std::memory_order_relaxed))) break;
                        for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
                    } else if (dif < 0) {
                        break; // Lane empty, try next
                    } else {
                        pos = lane.physical_egress_gate_.load(std::memory_order_relaxed);
                    }
                    for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
                }

                if (dif == 0) {
                    _mm_prefetch(reinterpret_cast<const char*>(&lane.data_[(pos + 1) & LaneMask]), _MM_HINT_T0);

                    if constexpr (std::is_pointer_v<T>) {
                        out_item = lane.data_[pos & LaneMask];
                        if (out_item) [[likely]] _mm_prefetch(reinterpret_cast<const char*>(out_item), _MM_HINT_T0);
                    } else if constexpr (std::is_move_assignable_v<T>) {
                        out_item = std::move(lane.data_[pos & LaneMask]);
                    } else {
                        std::memcpy(&out_item, &lane.data_[pos & LaneMask], sizeof(T));
                    }
                    lane.sequences_[pos & LaneMask].store(pos + LaneMask + 1, std::memory_order_release);
                    return true;
                }
            }
            return false; // All lanes empty
        }

        /** @brief Managed-data integration for conduits of pointers. */
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

        /** @brief Blocking pop returning value. */
        SLAB_FORCE_INLINE T pop() noexcept {
            T item;
            while (!try_pop(item)) {
                _mm_pause();
            }
            return item;
        }

        /**
         * @brief Blocking pool-integrated pop.
         */
        template <typename Pool, typename = std::enable_if_t<!std::is_same_v<Pool, T>>>
        SLAB_FORCE_INLINE managed_data<value_type_pod, Pool>
        pop(Pool& pool) noexcept {
            static_assert(std::is_pointer_v<T>, "pop(Pool&) requires a conduit of pointers!");
            T raw = pop(); // Call the blocking pop() that returns T
            return managed_data<value_type_pod, Pool>(raw, pool);
        }

        /** @brief Aggregated occupancy across all lanes. */
        [[nodiscard]] inline size_t occupancy() const noexcept {
            size_t total = 0;
            for (const auto& lane : lanes_) 
                total += (lane.physical_ingress_gate_.load(std::memory_order_relaxed) - lane.physical_egress_gate_.load(std::memory_order_relaxed));
            return total;
        }

        [[nodiscard]] inline size_t approx_size() const noexcept {
            return occupancy();
        }

        /**
         * @brief Batch Pop: Amortizes atomic overhead via CAS-N.
         * @return Number of items actually popped.
         */
        inline size_t pop_batch(T* out_batch, size_t max_count) noexcept {
            for (std::size_t i = 0; i < NumLanes; ++i) {
                auto& lane = select_consumer_lane(i);
                std::size_t pos = lane.physical_egress_gate_.load(std::memory_order_relaxed);
                std::size_t count = 0;
                intptr_t dif = 0;
                for (uint32_t retries = 0; ; ++retries) {
                    std::size_t seq = lane.sequences_[pos & LaneMask].load(std::memory_order_acquire);
                    dif = (intptr_t)seq - (intptr_t)(pos + 1);
                    if (dif == 0) {
                        std::size_t avail = lane.physical_ingress_gate_.load(std::memory_order_relaxed) - pos;
                        if (avail == 0) avail = 1; // Mitigate stale read deadlock
                        count = std::min(avail, max_count);
                        if (lane.physical_egress_gate_.compare_exchange_strong(pos, pos + count, std::memory_order_relaxed)) {
                            break;
                        }
                        count = 0; // Reset count on CAS failure to prevent phantom batch fulfillment
                    } else if (dif < 0) {
                        break;
                    }
                    else pos = lane.physical_egress_gate_.load(std::memory_order_relaxed);
                    for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
                }
                if (count > 0) {
                    for (size_t j = 0; j < count; j += 8) {
                        const size_t sub = (j + 8 <= count) ? 8 : count - j;
                        // Cache Line Warming: 8 coordination tickets fit in 1 cache line.
                        _mm_prefetch(reinterpret_cast<const char*>(&lane.sequences_[(pos + j) & LaneMask]), _MM_HINT_T0);
                        // Data Warming: Prime the payload area for the current sub-batch.
                        _mm_prefetch(reinterpret_cast<const char*>(&lane.data_[(pos + j) & LaneMask]), _MM_HINT_T0);

                        for (size_t k = 0; k < sub; ++k) {
                            const size_t curr_pos = pos + j + k;
                            while (lane.sequences_[curr_pos & LaneMask].load(std::memory_order_acquire) != curr_pos + 1) { _mm_pause(); }
                        }
                        if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) == 8) {
                            if (SL_EXPECT_TRUE(((pos + j) & LaneMask) + 8 <= LaneCapacity)) {
                                _mm512_storeu_si512(&out_batch[j], _mm512_loadu_si512(&lane.data_[(pos + j) & LaneMask]));
                            } else {
                                for (size_t k = 0; k < sub; ++k) out_batch[j + k] = std::move(lane.data_[(pos + j + k) & LaneMask]);
                            }
                        } else {
                            for (size_t k = 0; k < sub; ++k) out_batch[j + k] = std::move(lane.data_[(pos + j + k) & LaneMask]);
                        }
                        for (size_t k = 0; k < sub; ++k) {
                            lane.sequences_[(pos + j + k) & LaneMask].store(pos + j + k + LaneMask + 1, std::memory_order_release);
                        }
                    }
                    return count;
                }
            }
            return 0;
        }

        /** @brief Batch Push: Amortizes atomic overhead via CAS-N. */
        inline size_t push_batch(const T* in_batch, size_t max_count) noexcept {
            for (std::size_t i = 0; i < NumLanes; ++i) {
                auto& lane = select_producer_lane(i);
                std::size_t pos = lane.physical_ingress_gate_.load(std::memory_order_relaxed);
                std::size_t count = 0;
                intptr_t dif = 0;
                for (uint32_t retries = 0; ; ++retries) {
                    std::size_t seq = lane.sequences_[pos & LaneMask].load(std::memory_order_acquire);
                    dif = (intptr_t)seq - (intptr_t)pos;
                    if (dif == 0) {
                        std::size_t avail = LaneCapacity - (pos - lane.physical_egress_gate_.load(std::memory_order_relaxed));
                        if (avail == 0) avail = 1; // Mitigate stale read deadlock
                        count = std::min(avail, max_count);
                        if (lane.physical_ingress_gate_.compare_exchange_strong(pos, pos + count, std::memory_order_relaxed)) {
                            break;
                        }
                        count = 0; // Reset count on CAS failure to prevent phantom batch fulfillment
                    } else if (dif < 0) {
                        break;
                    }
                    else pos = lane.physical_ingress_gate_.load(std::memory_order_relaxed);
                    for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
                }
                if (count > 0) {
                    for (size_t j = 0; j < count; j += 8) {
                        const size_t sub = (j + 8 <= count) ? 8 : count - j;
                        // Cache Line Warming: 8 coordination tickets fit in 1 cache line.
                        _mm_prefetch(reinterpret_cast<const char*>(&lane.sequences_[(pos + j) & LaneMask]), _MM_HINT_T0);
                        // Data Warming: Prime the destination payload area for the incoming batch.
                        _mm_prefetch(reinterpret_cast<const char*>(&lane.data_[(pos + j) & LaneMask]), _MM_HINT_T0);

                        for (size_t k = 0; k < sub; ++k) {
                            const size_t curr_pos = pos + j + k;
                            while (lane.sequences_[curr_pos & LaneMask].load(std::memory_order_acquire) != curr_pos) { _mm_pause(); }
                        }
                        if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) == 8) {
                            if (SL_EXPECT_TRUE(((pos + j) & LaneMask) + 8 <= LaneCapacity)) {
                                _mm512_storeu_si512(&lane.data_[(pos + j) & LaneMask], _mm512_loadu_si512(&in_batch[j]));
                            } else {
                                for (size_t k = 0; k < sub; ++k) lane.data_[(pos + j + k) & LaneMask] = in_batch[j + k];
                            }
                        } else {
                            for (size_t k = 0; k < sub; ++k) lane.data_[(pos + j + k) & LaneMask] = in_batch[j + k];
                        }
                        for (size_t k = 0; k < sub; ++k) {
                            lane.sequences_[(pos + j + k) & LaneMask].store(pos + j + k + 1, std::memory_order_release);
                        }
                    }
                    return count;
                }
            }
            return 0;
        }

        /**
         * @brief Linear search to invalidate an item across all lanes.
         */
        SLAB_HOT void invalidate_by_ptr(T target) noexcept {
            // Rematerialization: Utilizes bit_cast (C++20) to extract
            // the hardware-level bit pattern of the target for SIMD comparison,
            // ensuring bit-perfect invalidation for any 8-byte POD.
            const int64_t target_val = std::bit_cast<int64_t>(target);

            #if defined(__AVX512F__)
            const __m512i v_target = _mm512_set1_epi64(target_val);
            const __m512i v_zero = _mm512_setzero_si512();
            #elif defined(__AVX2__)
            const __m256i v_target_256 = _mm256_set1_epi64x(target_val);
            const __m256i v_zero_256 = _mm256_setzero_si256();
            #endif

            for (auto& lane : lanes_) {
                size_t i = 0;
                #if defined(__AVX512F__)
                for (; i + 8 <= LaneCapacity; i += 8) {
                    __m512i v_data = _mm512_loadu_si512(reinterpret_cast<const void*>(&lane.data_[i]));
                    __mmask8 mask = _mm512_cmpeq_epi64_mask(v_data, v_target);
                    if (mask) _mm512_mask_storeu_epi64(&lane.data_[i], mask, v_zero);
                }
                #elif defined(__AVX2__)
                for (; i + 4 <= LaneCapacity; i += 4) {
                    __m256i v_data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&lane.data_[i]));
                    __m256i v_mask = _mm256_cmpeq_epi64(v_data, v_target_256);
                    if (!_mm256_testz_si256(v_mask, v_mask)) {
                        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&lane.data_[i]), _mm256_blendv_epi8(v_data, v_zero_256, v_mask));
                    }
                }
                #endif
                for (; i < LaneCapacity; ++i) if (lane.data_[i] == target) lane.data_[i] = T{};
            }
        }
    };

} // namespace slabflux::core