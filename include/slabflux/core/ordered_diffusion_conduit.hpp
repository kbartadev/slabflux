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
 * ============================================================================* @file ordered_diffusion_conduit.hpp
 * @brief Lock-Free, SIMD-Swept Ordered MPMC Matrix.
 * 
 * High-Performance Design:
 * 1. Cursor-Guided Diffusion: Replaces strict `fetch_add` ticket queues with 
 *    a lazy monotonic cursor. Threads sweep ahead of the cursor using AVX2,
 *    claiming contiguous chunks of memory simultaneously.
 * 2. Strict FIFO Progression: Bitmask clipping ensures threads cannot claim
 *    slots behind the active wavefront, perfectly preserving chronological ordering.
 * 3. Head-of-Line Bypass: If Thread A stalls writing slot 0, Thread B can 
 *    still pop slot 1 without stalling, delivering true wait-free performance.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <immintrin.h>
#include <thread>
#include <system_error>
#include <bit>
#include <cstring>

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/platform/os.hpp"
#include "slabflux/core/managed_data.hpp"
#include "slabflux/core/scoped_ptr.hpp"

namespace slabflux::core {

    template <typename T, std::size_t RequestedSize>
    class alignas(64) ordered_diffusion_conduit {
    public:
        using value_type = T;
        using value_type_pod = std::remove_pointer_t<T>;

        static constexpr std::size_t Capacity = (RequestedSize < 32) ? 32 : std::bit_ceil(RequestedSize);
        static_assert((Capacity % 32) == 0, "Capacity must be a multiple of 32 for AVX2 alignment.");
        static constexpr std::size_t Mask = Capacity - 1;

    private:
        static constexpr uint8_t STATE_EMPTY    = 0;
        static constexpr uint8_t STATE_RESERVED = 1;
        static constexpr uint8_t STATE_READY    = 2;
        static constexpr uint8_t STATE_CLAIMED  = 3;

        // The 1-byte state matrix
        alignas(64) std::atomic<uint8_t>* meta_{nullptr};
        alignas(64) T* data_{nullptr};
        std::size_t total_bytes_{0};

        // Lazy Hint Cursors (Monotonically increasing sequence numbers)
        alignas(64) std::atomic<size_t> ingress_cursor_{0};
        alignas(64) std::atomic<size_t> egress_cursor_{0};

        static SLAB_FORCE_INLINE void advance_cursor(std::atomic<size_t>& cursor, size_t new_val) noexcept {
            size_t current = cursor.load(std::memory_order_relaxed);
            while (static_cast<intptr_t>(new_val - current) > 0) {
                if (cursor.compare_exchange_weak(current, new_val, std::memory_order_relaxed)) {
                    break;
                }
            }
        }

    public:
        ordered_diffusion_conduit() {
            const size_t meta_bytes = (Capacity * sizeof(std::atomic<uint8_t>) + 63) & ~63;
            const size_t data_bytes = (Capacity * sizeof(T) + 63) & ~63;
            total_bytes_ = meta_bytes + data_bytes;

#ifndef _WIN32
            int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED | MAP_HUGETLB | MAP_HUGE_2MB;
            void* mem = ::mmap(nullptr, total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
            
            if (mem == MAP_FAILED) {
                flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED;
                mem = ::mmap(nullptr, total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
            }
            if (mem == MAP_FAILED) {
                flags = MAP_PRIVATE | MAP_ANONYMOUS;
                mem = ::mmap(nullptr, total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
                if (mem == MAP_FAILED) throw std::bad_alloc();
            }
            ::madvise(mem, total_bytes_, MADV_HUGEPAGE | MADV_DONTDUMP);
            ::mlock(mem, total_bytes_);
#else
            void* mem = ::VirtualAlloc(nullptr, total_bytes_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!mem) throw std::bad_alloc();
#endif

            meta_ = static_cast<std::atomic<uint8_t>*>(mem);
            data_ = reinterpret_cast<T*>(static_cast<char*>(mem) + meta_bytes);

            for (std::size_t i = 0; i < Capacity; ++i) {
                new (&meta_[i]) std::atomic<uint8_t>(STATE_EMPTY);
            }
        }

        ~ordered_diffusion_conduit() {
            if (meta_) {
#ifndef _WIN32
                ::munlock(meta_, total_bytes_);
                ::munmap(meta_, total_bytes_);
#else
                ::VirtualFree(meta_, 0, MEM_RELEASE);
#endif
            }
        }

        ordered_diffusion_conduit(const ordered_diffusion_conduit&) = delete;
        ordered_diffusion_conduit& operator=(const ordered_diffusion_conduit&) = delete;

        /**
         * @brief Sequentially Ordered AVX2 Push.
         */
        SLAB_FORCE_INLINE bool try_push(const T& item) noexcept {
            size_t start_idx = ingress_cursor_.load(std::memory_order_relaxed);
            const __m256i v_empty = _mm256_setzero_si256(); // STATE_EMPTY == 0

            for (size_t offset = 0; offset < Capacity; offset += 32) {
                size_t current_logical = start_idx + offset;
                size_t logical_base = current_logical & ~31ULL;
                size_t physical_base = logical_base & Mask;
                
                // Ignore bits that fall behind the active cursor line
                uint32_t bit_offset = (offset == 0) ? (current_logical & 31) : 0;

                // SIMD Sweep
                __m256i v_states = _mm256_load_si256(reinterpret_cast<const __m256i*>(&meta_[physical_base]));
                uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v_states, v_empty));
                
                mask &= ~((1ULL << bit_offset) - 1);

                while (mask != 0) {
                    #if defined(_MSC_VER)
                    unsigned long bit; _BitScanForward(&bit, mask);
                    #else
                    uint32_t bit = __builtin_ctz(mask);
                    #endif

                    size_t physical_target = (physical_base + bit) & Mask;
                    size_t logical_target = logical_base + bit;

                    uint8_t expected = STATE_EMPTY;
                    if (SL_EXPECT_TRUE(meta_[physical_target].compare_exchange_strong(expected, STATE_RESERVED, std::memory_order_acquire))) {
                        
                        data_[physical_target] = item;
                        meta_[physical_target].store(STATE_READY, std::memory_order_release);
                        
                        // Lazily pull the global cursor forward so the next thread starts scanning from here
                        advance_cursor(ingress_cursor_, logical_target + 1);
                        return true;
                    }
                    mask &= (mask - 1); // Slot stolen by a peer, scan next available bit instantly
                }
            }
            return false;
        }

        template<typename U = T> requires (!std::is_pointer_v<U>)
        SLAB_FORCE_INLINE bool try_push(T* item) noexcept {
            return item ? try_push(*item) : false;
        }

        SLAB_FORCE_INLINE bool push(const T& item) noexcept {
            while (SL_EXPECT_FALSE(!try_push(item))) _mm_pause();
            return true;
        }

        /**
         * @brief Sequentially Ordered AVX2 Pop.
         */
        SLAB_FORCE_INLINE bool try_pop(T& out_item) noexcept {
            size_t start_idx = egress_cursor_.load(std::memory_order_relaxed);
            const __m256i v_ready = _mm256_set1_epi8(STATE_READY);

            for (size_t offset = 0; offset < Capacity; offset += 32) {
                size_t current_logical = start_idx + offset;
                size_t logical_base = current_logical & ~31ULL;
                size_t physical_base = logical_base & Mask;
                
                uint32_t bit_offset = (offset == 0) ? (current_logical & 31) : 0;

                // SIMD Sweep
                __m256i v_states = _mm256_load_si256(reinterpret_cast<const __m256i*>(&meta_[physical_base]));
                uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v_states, v_ready));
                
                mask &= ~((1ULL << bit_offset) - 1);

                while (mask != 0) {
                    #if defined(_MSC_VER)
                    unsigned long bit; _BitScanForward(&bit, mask);
                    #else
                    uint32_t bit = __builtin_ctz(mask);
                    #endif

                    size_t physical_target = (physical_base + bit) & Mask;
                    size_t logical_target = logical_base + bit;

                    uint8_t expected = STATE_READY;
                    if (SL_EXPECT_TRUE(meta_[physical_target].compare_exchange_strong(expected, STATE_CLAIMED, std::memory_order_acquire))) {
                        
                        if constexpr (std::is_pointer_v<T>) {
                            out_item = data_[physical_target];
                        } else if constexpr (std::is_move_assignable_v<T>) {
                            out_item = std::move(data_[physical_target]);
                        } else {
                            std::memcpy(&out_item, &data_[physical_target], sizeof(T));
                        }

                        meta_[physical_target].store(STATE_EMPTY, std::memory_order_release);
                        advance_cursor(egress_cursor_, logical_target + 1);
                        return true;
                    }
                    mask &= (mask - 1);
                }
            }
            return false;
        }

        SLAB_FORCE_INLINE T pop() noexcept {
            T item;
            while (!try_pop(item)) _mm_pause();
            return item;
        }

        /**
         * @brief Hardware matrix probe.
         */
        [[nodiscard]] size_t occupancy() const noexcept {
            size_t count = 0;
            const __m256i v_ready = _mm256_set1_epi8(STATE_READY);
            const __m256i v_claimed = _mm256_set1_epi8(STATE_CLAIMED);

            for (size_t i = 0; i < Capacity; i += 32) {
                __m256i v_states = _mm256_load_si256(reinterpret_cast<const __m256i*>(&meta_[i]));
                
                __m256i cmp_ready = _mm256_cmpeq_epi8(v_states, v_ready);
                __m256i cmp_claimed = _mm256_cmpeq_epi8(v_states, v_claimed);
                
                uint32_t mask_ready = _mm256_movemask_epi8(cmp_ready);
                uint32_t mask_claimed = _mm256_movemask_epi8(cmp_claimed);

                #if defined(_MSC_VER)
                count += __popcnt(mask_ready | mask_claimed);
                #else
                count += __builtin_popcount(mask_ready | mask_claimed);
                #endif
            }
            return count;
        }
        
        [[nodiscard]] size_t approx_size() const noexcept { return occupancy(); }

        // Integration Hooks for managed_data
        template <typename Pool, typename = std::enable_if_t<!std::is_same_v<Pool, T>>>
        SLAB_FORCE_INLINE managed_data<value_type_pod, Pool> try_pop(Pool& pool) noexcept {
            T raw;
            if (try_pop(raw)) return managed_data<value_type_pod, Pool>(raw, pool);
            return managed_data<value_type_pod, Pool>();
        }

        template <typename Pool>
        SLAB_FORCE_INLINE bool try_push(managed_data<value_type_pod, Pool>& item) noexcept {
            if constexpr (std::is_pointer_v<T>) {
                T ptr = item.get();
                if (try_push(ptr)) { item.release(); return true; }
            } else {
                if (try_push(*item)) return true;
            }
            return false;
        }
    };

} // namespace slabflux::core