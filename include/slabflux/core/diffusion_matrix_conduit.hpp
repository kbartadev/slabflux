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
 * ============================================================================* @file diffusion_matrix_conduit.hpp
 * @brief AVX2/AVX-512 Swept Statically-Indexed Matrix.
 * 
 * High-Performance Design:
 * 1. Diffusion Crossbar: Eliminates 8-byte sequence tickets per slot. Uses a 
 *    compressed 1-byte state matrix mapped directly into YMM/ZMM SIMD registers.
 * 2. Ordered Wavefront: Producers and consumers use a shared cursor to scan 
 *    forward using `_mm256_movemask_epi8`. Mathematical clipping guarantees 
 *    strict macro-FIFO progression while allowing Head-of-Line Bypass.
 * 3. Static Targeting: Bypasses dynamic lane routing for use in deterministic, 
 *    hardcoded cross-core topologies.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <immintrin.h>
#include <thread>
#include <system_error>
#include <bit>
#include <cstring>
#include <array>
#include <algorithm>

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/platform/os.hpp"

namespace slabflux::core {

    template <POD T, std::size_t RequestedSize, std::size_t NumLanes = 8>
    class alignas(64) diffusion_matrix_conduit {
    public:
        using value_type = T;
        using value_type_pod = std::remove_pointer_t<T>;

    private:
        static_assert((NumLanes & (NumLanes - 1)) == 0, "NumLanes must be power-of-two");

        static constexpr std::size_t MinTotal = NumLanes * 32; // Require at least 32 slots per lane for AVX2
        static constexpr std::size_t TotalSize = (RequestedSize < MinTotal) ? MinTotal : std::bit_ceil(RequestedSize);
        static constexpr std::size_t LaneCapacity = TotalSize / NumLanes;
        
        static_assert((LaneCapacity % 32) == 0, "Lane capacity must be a multiple of 32 for AVX2 alignment.");
        static constexpr std::size_t LaneMask = LaneCapacity - 1;
        static constexpr std::size_t LanesMask = NumLanes - 1;

        static constexpr uint8_t STATE_EMPTY    = 0;
        static constexpr uint8_t STATE_RESERVED = 1;
        static constexpr uint8_t STATE_READY    = 2;
        static constexpr uint8_t STATE_CLAIMED  = 3;

        struct alignas(64) matrix_lane {
            // 1-Byte Compressed Matrix for SIMD sweeps
            alignas(64) std::atomic<uint8_t>* states_{nullptr}; 
            T* data_{nullptr};                                  
            std::size_t total_bytes_{ 0 };                      

            // Lazy Hint Cursors
            alignas(64) std::atomic<size_t> ingress_cursor_{0};
            alignas(64) std::atomic<size_t> egress_cursor_{0};
        };

        std::array<matrix_lane, NumLanes> lanes_;

        static SLAB_FORCE_INLINE void advance_cursor(std::atomic<size_t>& cursor, size_t new_val) noexcept {
            size_t current = cursor.load(std::memory_order_relaxed);
            while (static_cast<intptr_t>(new_val - current) > 0) {
                if (cursor.compare_exchange_weak(current, new_val, std::memory_order_relaxed)) {
                    break;
                }
            }
        }

    public:
        diffusion_matrix_conduit() {
            for (std::size_t l = 0; l < NumLanes; ++l) {
                auto& lane = lanes_[l];
                const size_t state_bytes = (LaneCapacity * sizeof(std::atomic<uint8_t>) + 63) & ~63;
                const size_t data_bytes = (LaneCapacity * sizeof(T) + 63) & ~63;
                lane.total_bytes_ = state_bytes + data_bytes;

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
                
                lane.states_ = static_cast<std::atomic<uint8_t>*>(mem);
                lane.data_ = reinterpret_cast<T*>(static_cast<char*>(mem) + state_bytes);

                for (std::size_t i = 0; i < LaneCapacity; ++i) {
                    new (&lane.states_[i]) std::atomic<uint8_t>(STATE_EMPTY);
                }
            }
        }

        ~diffusion_matrix_conduit() {
            for (auto& lane : lanes_) {
                if (lane.states_) {
                    #ifndef _WIN32
                    ::munlock(lane.states_, lane.total_bytes_);
                    ::munmap(lane.states_, lane.total_bytes_);
                    #else
                    ::VirtualFree(lane.states_, 0, MEM_RELEASE);
                    #endif
                }
            }
        }

        diffusion_matrix_conduit(const diffusion_matrix_conduit&) = delete;
        diffusion_matrix_conduit& operator=(const diffusion_matrix_conduit&) = delete;

        /**
         * @brief Statically targeted non-blocking push utilizing AVX2 forward diffusion.
         */
        SLAB_FORCE_INLINE bool try_push_lane(std::size_t lane_id, const T& item) noexcept {
            auto& lane = lanes_[lane_id & LanesMask];
            size_t start_idx = lane.ingress_cursor_.load(std::memory_order_relaxed);
            const __m256i v_empty = _mm256_setzero_si256(); // STATE_EMPTY == 0

            for (size_t offset = 0; offset < LaneCapacity; offset += 32) {
                size_t current_logical = start_idx + offset;
                size_t logical_base = current_logical & ~31ULL;
                size_t physical_base = logical_base & LaneMask;
                
                uint32_t bit_offset = (offset == 0) ? (current_logical & 31) : 0;

                // SIMD Radar Sweep
                __m256i v_states = _mm256_load_si256(reinterpret_cast<const __m256i*>(&lane.states_[physical_base]));
                uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v_states, v_empty));
                mask &= ~((1ULL << bit_offset) - 1); // Clip bits behind the wavefront

                while (mask != 0) {
                    #if defined(_MSC_VER)
                    unsigned long bit; _BitScanForward(&bit, mask);
                    #else
                    uint32_t bit = __builtin_ctz(mask);
                    #endif

                    size_t physical_target = (physical_base + bit) & LaneMask;
                    size_t logical_target = logical_base + bit;

                    uint8_t expected = STATE_EMPTY;
                    if (SL_EXPECT_TRUE(lane.states_[physical_target].compare_exchange_strong(expected, STATE_RESERVED, std::memory_order_acquire))) {
                        lane.data_[physical_target] = item;
                        lane.states_[physical_target].store(STATE_READY, std::memory_order_release);
                        advance_cursor(lane.ingress_cursor_, logical_target + 1);
                        return true;
                    }
                    mask &= (mask - 1); // Slot stolen by a peer, instantaneously check next bit
                }
            }
            return false;
        }

        /**
         * @brief Statically targeted vector batch pop.
         */
        SLAB_FORCE_INLINE std::size_t pop_batch_lane(std::size_t lane_id, T* out_batch, std::size_t max_count) noexcept {
            auto& lane = lanes_[lane_id & LanesMask];
            size_t start_idx = lane.egress_cursor_.load(std::memory_order_relaxed);
            const __m256i v_ready = _mm256_set1_epi8(STATE_READY);
            size_t popped = 0;

            for (size_t offset = 0; offset < LaneCapacity && popped < max_count; offset += 32) {
                size_t current_logical = start_idx + offset;
                size_t logical_base = current_logical & ~31ULL;
                size_t physical_base = logical_base & LaneMask;
                
                uint32_t bit_offset = (offset == 0) ? (current_logical & 31) : 0;

                __m256i v_states = _mm256_load_si256(reinterpret_cast<const __m256i*>(&lane.states_[physical_base]));
                uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v_states, v_ready));
                mask &= ~((1ULL << bit_offset) - 1);

                if (mask == 0) break; // Entire 32-slot sector is dry; yield back to core

                // Head-of-Line bypass extraction
                while (mask != 0 && popped < max_count) {
                    #if defined(_MSC_VER)
                    unsigned long bit; _BitScanForward(&bit, mask);
                    #else
                    uint32_t bit = __builtin_ctz(mask);
                    #endif

                    size_t physical_target = (physical_base + bit) & LaneMask;
                    size_t logical_target = logical_base + bit;

                    uint8_t expected = STATE_READY;
                    if (SL_EXPECT_TRUE(lane.states_[physical_target].compare_exchange_strong(expected, STATE_CLAIMED, std::memory_order_acquire))) {
                        if constexpr (std::is_pointer_v<T>) {
                            out_batch[popped] = lane.data_[physical_target];
                        } else if constexpr (std::is_move_assignable_v<T>) {
                            out_batch[popped] = std::move(lane.data_[physical_target]);
                        } else {
                            std::memcpy(&out_batch[popped], &lane.data_[physical_target], sizeof(T));
                        }
                        lane.states_[physical_target].store(STATE_EMPTY, std::memory_order_release);
                        advance_cursor(lane.egress_cursor_, logical_target + 1);
                        popped++;
                    }
                    mask &= (mask - 1);
                }
            }
            return popped;
        }
    };

} // namespace slabflux::core