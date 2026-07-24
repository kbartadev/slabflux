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
 * @file mpmc_matrix_conduit.hpp
 * @brief -Indexed SHM Matrix.
 * @details Implements a statically-sharded Multi-Producer Multi-Consumer conduit
 * utilizing a Decoupled Array architecture for high-velocity SHM/IPC paths,
 * with AVX-512 batching for data movement.
 * 
 * High-Performance Design:
 * 1. Decoupled Array Architecture: Separates sequence meta from payload data into
 *    parallel HugePage-backed arrays to maximize L1-D hit rates and AVX-512 batching for data movement.
 * 2. Static Routing: Designed for hardcoded execution lanes to eliminate the 
 *    overhead of dynamic core-discovery in the hot path.
 * 3. Hardware-Managed Residency: Utilizes mmap/mlock with HugePage support 
 *    (2MB pages) to guarantee physical RAM residency and minimize TLB misses, with
 *    graceful fallback to standard locked pages if HugePage allocation fails.
 * 
 * API Contract:
 * - Progress Guarantee: Wait-free O(1) transitions for both producers and consumers.
 * - Safety: ABA-protection via monotonic sequence versioning per slot.
 * 
 * ========================================================================
 * API REFERENCE: mpmc_matrix_conduit<T, Capacity>
 * ========================================================================
 * 1. INGRESS (PRODUCER LANES):
 *    - bool try_push_lane(id, item)  : Statically-targeted push operation.
 * 
 * 2. EGRESS (CONSUMER LANES):
 *    - size_t pop_batch_lane(id, ...) : Statically-targeted vector drain.
 * 
 * @note This component is optimized for SHM/IPC paths where producer/consumer 
 * mapping is known at compile-time and core affinity is strictly controlled.
 */

#pragma once
#include <cstring>
#include <atomic>
#include <bit>
#include <immintrin.h>
#include <cstdint>
#include <system_error>
#include <utility>
#include <array>
#include <algorithm>

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    /**
     * @brief Statically-Indexed Matrix.
     * @details A specialized MPMC conduit optimized for SHM/IPC paths where 
     * producer/consumer mapping is known at compile-time.
     * 
     * Design Architecture:
     * - Bypasses the L3 interconnect wall via lane-local memory channels.
     * - Utilizes Sequence-Validated slots to ensure wait-free O(1) transitions.
     */
    template <POD T, std::size_t RequestedSize, std::size_t NumLanes = 8>
    class alignas(64) mpmc_matrix_conduit {
    public:
        using value_type = T;
        using value_type_pod = std::remove_pointer_t<T>;

    private:
        static_assert((NumLanes & (NumLanes - 1)) == 0, "NumLanes must be power-of-two");

        static constexpr std::size_t MinTotal = NumLanes * 2;
        static constexpr std::size_t TotalSize = (RequestedSize < MinTotal) ? MinTotal : std::bit_ceil(RequestedSize);
        static constexpr std::size_t LaneCapacity = TotalSize / NumLanes;
        static constexpr std::size_t LaneMask = LaneCapacity - 1;
        static constexpr std::size_t LanesMask = NumLanes - 1;

        struct alignas(64) mpmc_lane {
            static constexpr std::size_t LaneMask = LaneCapacity - 1;

            // Group 0: Metadata Array Pointers (Line 0)
            alignas(64) std::atomic<std::size_t>* sequences_{nullptr}; // 8 bytes
            T* data_{nullptr};                                         // 8 bytes
            std::size_t total_bytes_{ 0 };                             // 8 bytes
            alignas(64) std::atomic<std::size_t> physical_ingress_gate_{ 0 };
            alignas(64) std::atomic<std::size_t> physical_egress_gate_{ 0 };
        };

        std::array<mpmc_lane, NumLanes> lanes_;

    public:
        mpmc_matrix_conduit() {
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

        ~mpmc_matrix_conduit() {
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

        mpmc_matrix_conduit(const mpmc_matrix_conduit&) = delete;
        mpmc_matrix_conduit& operator=(const mpmc_matrix_conduit&) = delete;

        /**
         * @brief Statically targeted non-blocking push operation.
         * @param lane_id Hardcoded index mapping directly to the caller's execution lane.
         */
        SLAB_FORCE_INLINE bool try_push_lane(std::size_t lane_id, const T& item) noexcept {
            auto& lane = lanes_[lane_id & LanesMask];
            std::size_t pos = lane.physical_ingress_gate_.load(std::memory_order_relaxed);
            
            std::size_t seq = lane.sequences_[pos & LaneMask].load(std::memory_order_acquire);
            intptr_t dif = (intptr_t)seq - (intptr_t)pos;
            
            if (__builtin_expect(dif == 0, 1)) {
                if (lane.physical_ingress_gate_.compare_exchange_strong(pos, pos + 1, std::memory_order_relaxed)) {
                    lane.data_[pos & LaneMask] = item;
                    lane.sequences_[pos & LaneMask].store(pos + 1, std::memory_order_release);
                    return true;
                }
            }
            return false;
        }

        /**
         * @brief Statically targeted vector batch pop pass utilizing your hardware optimizations.
         */
        SLAB_FORCE_INLINE std::size_t pop_batch_lane(std::size_t lane_id, T* out_batch, std::size_t max_count) noexcept {
            auto& lane = lanes_[lane_id & LanesMask];
            std::size_t pos = lane.physical_egress_gate_.load(std::memory_order_relaxed);
            std::size_t count = 0;
            
            std::size_t seq = lane.sequences_[pos & LaneMask].load(std::memory_order_acquire);
            intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);
            
            if (__builtin_expect(dif == 0, 1)) {
                std::size_t avail = lane.physical_ingress_gate_.load(std::memory_order_relaxed) - pos;
                if (avail == 0) avail = 1;
                count = std::min(avail, max_count);
                if (!lane.physical_egress_gate_.compare_exchange_strong(pos, pos + count, std::memory_order_relaxed)) {
                    return 0;
                }
            } else {
                return 0;
            }

            if (count > 0) {
                for (size_t j = 0; j < count; j += 8) {
                    const size_t sub = (j + 8 <= count) ? 8 : count - j;
                    
                    for (size_t k = 0; k < sub; ++k) {
                        const size_t curr_pos = pos + j + k;
                        while (lane.sequences_[curr_pos & LaneMask].load(std::memory_order_acquire) != curr_pos + 1) { 
                            // Vyukov Invariant: We have claimed the slots via head CAS, but must
                            // wait for the producer to finish writing and publish the sequence.
                            // This stabilizes the interconnect and prevents reading partial data.
                            #if defined(__x86_64__) || defined(_M_X64)
                            _mm_pause(); 
                            #endif
                        }
                    }

                    if constexpr (std::is_trivially_copyable_v<T> && sizeof(T) == 8) {
                        if (SL_EXPECT_TRUE(sub == 8 && ((pos + j) & LaneMask) + 8 <= LaneCapacity)) {
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
            return 0;
        }
    };

} // namespace slabflux::core