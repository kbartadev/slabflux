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
 * ============================================================================* @file spatial_diffusion_conduit.hpp
 * @brief Lock-Free, Headless MPMC Matrix utilizing AVX2/AVX-512 Spatial Diffusion.
 * 
 * High-Performance Design:
 * 1. Data Stationarity: Payloads are written exactly once into a pre-allocated HugePage 
 *    arena and never physically move. 
 * 2. Spatial Diffusion: Eliminates the central `head` and `tail` atomic counters. 
 *    Threads scatter across the matrix and use SIMD vector instructions to sweep 
 *    32-64 slots per clock cycle to discover available work.
 * 3. Zero-RFO Bottleneck: Because threads don't share a monotonic counter, MESI 
 *    cache-line bouncing is mathematically eliminated.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <immintrin.h>
#include <thread>
#include <system_error>
#include <functional>
#include <bit>

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/platform/os.hpp"
#include "slabflux/core/managed_data.hpp"
#include "slabflux/core/scoped_ptr.hpp"

namespace slabflux::core {

    template <typename T, std::size_t RequestedSize>
    class alignas(64) spatial_diffusion_conduit {
    public:
        using value_type = T;
        using value_type_pod = std::remove_pointer_t<T>;

        // Enforce alignment to 32 for clean AVX2 256-bit register loads
        static constexpr std::size_t Capacity = (RequestedSize < 32) ? 32 : std::bit_ceil(RequestedSize);
        static_assert((Capacity % 32) == 0, "Capacity must be a multiple of 32 for AVX2 alignment.");
        static constexpr std::size_t Mask = Capacity - 1;

    private:
        // STATE MACHINE TOKENS
        static constexpr uint8_t STATE_EMPTY    = 0;
        static constexpr uint8_t STATE_RESERVED = 1; // Producer is writing
        static constexpr uint8_t STATE_READY    = 2; // Data is ready for Consumer
        static constexpr uint8_t STATE_CLAIMED  = 3; // Consumer is reading

        // The Metadata Grid: 1 byte per slot. 
        // AVX2 scans 32 slots in a single instruction.
        alignas(64) std::atomic<uint8_t>* meta_{nullptr};
        
        // The Stationary Data Arena
        alignas(64) T* data_{nullptr};
        
        std::size_t total_bytes_{0};

        /**
         * @brief Scatters threads dynamically across the matrix.
         * @details Provides a unique 32-aligned starting sector for every thread
         * to guarantee immediate physical diffusion and zero Day-1 contention.
         */
        static SLAB_FORCE_INLINE size_t get_diffused_cursor() noexcept {
            thread_local size_t cursor = (std::hash<std::thread::id>{}(std::this_thread::get_id()) % (Capacity / 32)) * 32;
            return cursor;
        }

    public:
        spatial_diffusion_conduit() {
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

        ~spatial_diffusion_conduit() {
            if (meta_) {
#ifndef _WIN32
                ::munlock(meta_, total_bytes_);
                ::munmap(meta_, total_bytes_);
#else
                ::VirtualFree(meta_, 0, MEM_RELEASE);
#endif
            }
        }

        spatial_diffusion_conduit(const spatial_diffusion_conduit&) = delete;
        spatial_diffusion_conduit& operator=(const spatial_diffusion_conduit&) = delete;

        /**
         * @brief Non-Blocking Producer Diffusion.
         * @details Scans 32 slots per cycle looking for an empty patch of memory.
         */
        SLAB_FORCE_INLINE bool try_push(const T& item) noexcept {
            size_t& cursor = get_diffused_cursor();
            const __m256i v_empty = _mm256_setzero_si256(); // Target state: 0 (EMPTY)

            // Sweep the entire matrix once
            for (size_t iter = 0; iter < Capacity; iter += 32) {
                size_t base_idx = (cursor + iter) & Mask;

                // 1. Radar Sweep: Load 32 state bytes instantly
                __m256i v_states = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&meta_[base_idx]));
                
                // 2. Identify all empty slots in this sector
                uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v_states, v_empty));

                while (mask != 0) {
                    // 3. Extract the first available slot using hardware bit-scan
                    #if defined(_MSC_VER)
                    unsigned long bit; _BitScanForward(&bit, mask);
                    #else
                    uint32_t bit = __builtin_ctz(mask);
                    #endif

                    size_t target_idx = base_idx + bit;
                    uint8_t expected = STATE_EMPTY;

                    // 4. Lock-Free Claim (Reserve the patch)
                    if (SL_EXPECT_TRUE(meta_[target_idx].compare_exchange_strong(expected, STATE_RESERVED, std::memory_order_acquire))) {
                        
                        // 5. Stationary Write
                        data_[target_idx] = item;
                        
                        // 6. Publish to Consumers
                        meta_[target_idx].store(STATE_READY, std::memory_order_release);
                        
                        // Advance cursor for next push to ensure continuous diffusion
                        cursor = (base_idx + 32) & Mask; 
                        return true;
                    }
                    
                    // CAS failed (another producer beat us). Clear this bit and try the next empty slot in the register.
                    mask &= (mask - 1); 
                }
            }
            
            return false; // Matrix is completely saturated
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
         * @brief Non-Blocking Consumer Diffusion.
         * @details Radars 32 slots per cycle looking for active, ready data.
         */
        SLAB_FORCE_INLINE bool try_pop(T& out_item) noexcept {
            size_t& cursor = get_diffused_cursor();
            const __m256i v_ready = _mm256_set1_epi8(STATE_READY);

            // Sweep the entire matrix once
            for (size_t iter = 0; iter < Capacity; iter += 32) {
                size_t base_idx = (cursor + iter) & Mask;

                // 1. Radar Sweep
                __m256i v_states = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&meta_[base_idx]));
                uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v_states, v_ready));

                while (mask != 0) {
                    #if defined(_MSC_VER)
                    unsigned long bit; _BitScanForward(&bit, mask);
                    #else
                    uint32_t bit = __builtin_ctz(mask);
                    #endif

                    size_t target_idx = base_idx + bit;
                    uint8_t expected = STATE_READY;

                    // 2. Lock-Free Claim (Take ownership of the data patch)
                    if (SL_EXPECT_TRUE(meta_[target_idx].compare_exchange_strong(expected, STATE_CLAIMED, std::memory_order_acquire))) {
                        
                        // 3. Stationary Read
                        if constexpr (std::is_pointer_v<T>) {
                            out_item = data_[target_idx];
                        } else if constexpr (std::is_move_assignable_v<T>) {
                            out_item = std::move(data_[target_idx]);
                        } else {
                            std::memcpy(&out_item, &data_[target_idx], sizeof(T));
                        }

                        // 4. Return to Void (Free the patch for producers)
                        meta_[target_idx].store(STATE_EMPTY, std::memory_order_release);
                        
                        cursor = (base_idx + 32) & Mask;
                        return true;
                    }
                    mask &= (mask - 1);
                }
            }
            return false; // Matrix is completely empty
        }

        SLAB_FORCE_INLINE T pop() noexcept {
            T item;
            while (!try_pop(item)) _mm_pause();
            return item;
        }

        /**
         * @brief AVX2 Hardware Matrix Size Probe.
         * @details Uses a rapid vector scan to estimate active occupancy 
         * without relying on a centralized bottleneck counter.
         */
        [[nodiscard]] size_t occupancy() const noexcept {
            size_t count = 0;
            const __m256i v_ready = _mm256_set1_epi8(STATE_READY);
            const __m256i v_claimed = _mm256_set1_epi8(STATE_CLAIMED);

            for (size_t i = 0; i < Capacity; i += 32) {
                __m256i v_states = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&meta_[i]));
                
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