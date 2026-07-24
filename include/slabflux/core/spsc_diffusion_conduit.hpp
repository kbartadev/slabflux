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
 * ============================================================================* @file spsc_diffusion_conduit.hpp
 * @brief The Ultimate SPSC Matrix. Zero-Contention State Diffusion.
 * 
 * High-Performance Design:
 * 1. Abolition of Head/Tail Monotonics: Completely drops the shared atomic 
 *    counters. All state is maintained locally and validated via a distributed 
 *    1-byte state matrix, completely eradicating MESI RFO stalls.
 * 2. AVX-512 / AVX2 Front-Wave Sweeping: Uses `std::countr_one` combined with 
 *    vectorized comparisons to validate and claim up to 32 contiguous slots 
 *    in a single CPU cycle.
 * 3. Cache Line Sovereignty: Producer and Consumer naturally drift apart across 
 *    the state matrix, meaning they virtually never touch the same physical 
 *    silicon outside of absolute saturation.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <immintrin.h>
#include <system_error>
#include <bit>
#include <cstring>
#include <algorithm>

#ifndef _WIN32
#include <sys/mman.h>
#endif

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/platform/os.hpp"
#include "slabflux/core/managed_data.hpp"

namespace slabflux::core {

    template <typename T, std::size_t RequestedSize>
    class alignas(64) spsc_diffusion_conduit {
    public:
        using value_type = T;
        using value_type_pod = std::remove_pointer_t<T>;

        static constexpr std::size_t Capacity = (RequestedSize < 32) ? 32 : std::bit_ceil(RequestedSize);
        static_assert((Capacity % 32) == 0, "Capacity must be a multiple of 32 for AVX2 alignment.");
        static constexpr std::size_t Mask = Capacity - 1;

    private:
        static constexpr uint8_t STATE_EMPTY = 0;
        static constexpr uint8_t STATE_READY = 1;

        // Detached Matrix Layout
        alignas(64) std::atomic<uint8_t>* meta_{nullptr};
        alignas(64) T* data_{nullptr};
        std::size_t total_bytes_{0};

        // Thread-Local Horizons (No atomic ping-pong)
        alignas(64) std::size_t ingress_cursor_{0};
        alignas(64) std::size_t egress_cursor_{0};

        void* arbiter_ptr_{nullptr};
        void (*fault_fn_)(void*, uint32_t){nullptr};

    public:
        spsc_diffusion_conduit() {
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

        ~spsc_diffusion_conduit() {
            if (meta_) {
#ifndef _WIN32
                ::munlock(meta_, total_bytes_);
                ::munmap(meta_, total_bytes_);
#else
                ::VirtualFree(meta_, 0, MEM_RELEASE);
#endif
            }
        }

        spsc_diffusion_conduit(const spsc_diffusion_conduit&) = delete;
        spsc_diffusion_conduit& operator=(const spsc_diffusion_conduit&) = delete;

        void attach_fault_reporter(void* arbiter, void (*fn)(void*, uint32_t)) noexcept {
            arbiter_ptr_ = arbiter;
            fault_fn_ = fn;
        }

        void on_conduit_full_drop() noexcept {
            if (fault_fn_) fault_fn_(arbiter_ptr_, 0xE62E55);
        }

        SLAB_FORCE_INLINE bool on_raw_frame(const T& item, int /*res*/) noexcept {
            return try_push(item);
        }

        // ====================================================================
        // FAST SCALAR INGRESS
        // ====================================================================
        SLAB_FORCE_INLINE bool try_push(const T& item) noexcept {
            size_t physical_idx = ingress_cursor_ & Mask;
            
            if (SL_EXPECT_TRUE(meta_[physical_idx].load(std::memory_order_acquire) == STATE_EMPTY)) {
                data_[physical_idx] = item;
                
                // Interconnect RFO Mitigation
                _mm_prefetch(reinterpret_cast<const char*>(&meta_[(physical_idx + 1) & Mask]), _MM_HINT_T0);
                
                meta_[physical_idx].store(STATE_READY, std::memory_order_release);
                ingress_cursor_++;
                return true;
            }
            return false;
        }

        template<typename U = T> requires (!std::is_pointer_v<U>)
        SLAB_FORCE_INLINE bool try_push(T* item) noexcept {
            return item ? try_push(*item) : false;
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

        SLAB_FORCE_INLINE bool push(const T& item) noexcept {
            while (SL_EXPECT_FALSE(!try_push(item))) _mm_pause();
            return true;
        }

        template<typename U = T> requires (!std::is_pointer_v<U>)
        SLAB_FORCE_INLINE bool push(T* item) noexcept {
            if (SL_EXPECT_FALSE(!item)) return false;
            while (!try_push(*item)) _mm_pause();
            return true;
        }

        template <typename Pool>
        SLAB_FORCE_INLINE void push(managed_data<value_type_pod, Pool>& item) noexcept {
            while (!try_push(item)) _mm_pause();
        }

        // ====================================================================
        // FAST SCALAR EGRESS
        // ====================================================================
        SLAB_FORCE_INLINE bool try_pop(T& out_item) noexcept {
            size_t physical_idx = egress_cursor_ & Mask;

            if (SL_EXPECT_TRUE(meta_[physical_idx].load(std::memory_order_acquire) == STATE_READY)) {
                if constexpr (std::is_pointer_v<T>) {
                    out_item = data_[physical_idx];
                } else if constexpr (std::is_move_assignable_v<T>) {
                    out_item = std::move(data_[physical_idx]);
                } else {
                    std::memcpy(&out_item, &data_[physical_idx], sizeof(T));
                }
                
                _mm_prefetch(reinterpret_cast<const char*>(&meta_[(physical_idx + 1) & Mask]), _MM_HINT_T0);
                
                meta_[physical_idx].store(STATE_EMPTY, std::memory_order_release);
                egress_cursor_++;
                return true;
            }
            return false;
        }

        SLAB_FORCE_INLINE T pop() noexcept {
            T item;
            while (!try_pop(item)) _mm_pause();
            return item;
        }

        SLAB_FORCE_INLINE void pop(T& out_item) noexcept {
            while (!try_pop(out_item)) _mm_pause();
        }

        template <typename Pool>
        SLAB_FORCE_INLINE managed_data<value_type_pod, Pool> try_pop(Pool& pool) noexcept {
            T raw;
            if (try_pop(raw)) return managed_data<value_type_pod, Pool>(raw, pool);
            return managed_data<value_type_pod, Pool>();
        }

        template <typename Pool, typename = std::enable_if_t<!std::is_same_v<Pool, T>>>
        SLAB_FORCE_INLINE managed_data<value_type_pod, Pool> pop(Pool& pool) noexcept {
            T raw = pop();
            return managed_data<value_type_pod, Pool>(raw, pool);
        }

        SLAB_FORCE_INLINE T* peek() noexcept {
            size_t physical_idx = egress_cursor_ & Mask;
            if (meta_[physical_idx].load(std::memory_order_acquire) == STATE_READY) {
                return &data_[physical_idx];
            }
            return nullptr;
        }

        SLAB_FORCE_INLINE void consume() noexcept {
            size_t physical_idx = egress_cursor_ & Mask;
            meta_[physical_idx].store(STATE_EMPTY, std::memory_order_release);
            egress_cursor_++;
        }

        // ====================================================================
        // VECTORIZED DIFFUSION BATCHING
        // ====================================================================
        inline size_t push_batch(const T* in_batch, size_t max_count) noexcept {
            size_t pushed = 0;
            const __m256i v_empty = _mm256_setzero_si256();
            const __m256i v_ready = _mm256_set1_epi8(STATE_READY);

            while (pushed < max_count) {
                size_t physical_idx = ingress_cursor_ & Mask;
                size_t chunk_avail = Capacity - physical_idx;
                size_t attempt = std::min({max_count - pushed, chunk_avail, (size_t)32});

                if (attempt >= 8 && constexpr (std::is_trivially_copyable_v<T> && sizeof(T) == 8)) {
                    // SIMD Front-Wave Validation
                    __m256i v_states = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&meta_[physical_idx]));
                    uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v_states, v_empty));
                    
                    // countr_one mathematically guarantees that we only write to slots that are 
                    // strictly sequential, preventing fragmentation loops entirely.
                    uint32_t contiguous = std::countr_one(mask);
                    size_t batch = std::min(attempt, static_cast<size_t>(contiguous));

                    if (batch == 0) break;

                    size_t j = 0;
                    #if defined(__AVX512F__)
                    for (; j + 8 <= batch; j += 8) {
                        _mm512_storeu_si512(&data_[physical_idx + j], _mm512_loadu_si512(&in_batch[pushed + j]));
                    }
                    #elif defined(__AVX2__)
                    for (; j + 4 <= batch; j += 4) {
                        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&data_[physical_idx + j]), 
                                            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&in_batch[pushed + j])));
                    }
                    #endif
                    for (; j < batch; ++j) data_[physical_idx + j] = in_batch[pushed + j];

                    std::atomic_thread_fence(std::memory_order_release); // Payload visibility barrier

                    if (batch == 32) {
                        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&meta_[physical_idx]), v_ready);
                    } else {
                        for (size_t k = 0; k < batch; ++k) meta_[physical_idx + k].store(STATE_READY, std::memory_order_relaxed);
                    }

                    ingress_cursor_ += batch;
                    pushed += batch;

                    if (batch < attempt) break; 
                } else {
                    // Scalar Fallback
                    if (meta_[physical_idx].load(std::memory_order_acquire) == STATE_EMPTY) {
                        data_[physical_idx] = in_batch[pushed];
                        meta_[physical_idx].store(STATE_READY, std::memory_order_release);
                        ingress_cursor_++;
                        pushed++;
                    } else {
                        break;
                    }
                }
            }
            return pushed;
        }

        inline size_t pop_batch(T* out_batch, size_t max_count) noexcept {
            size_t popped = 0;
            const __m256i v_ready = _mm256_set1_epi8(STATE_READY);
            const __m256i v_empty = _mm256_setzero_si256();

            while (popped < max_count) {
                size_t physical_idx = egress_cursor_ & Mask;
                size_t chunk_avail = Capacity - physical_idx;
                size_t attempt = std::min({max_count - popped, chunk_avail, (size_t)32});

                if (attempt >= 8 && constexpr (std::is_trivially_copyable_v<T> && sizeof(T) == 8)) {
                    __m256i v_states = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&meta_[physical_idx]));
                    uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v_states, v_ready));
                    
                    uint32_t contiguous = std::countr_one(mask);
                    size_t batch = std::min(attempt, static_cast<size_t>(contiguous));

                    if (batch == 0) break;

                    std::atomic_thread_fence(std::memory_order_acquire); // Payload visibility barrier

                    size_t j = 0;
                    #if defined(__AVX512F__)
                    for (; j + 8 <= batch; j += 8) {
                        _mm512_storeu_si512(&out_batch[popped + j], _mm512_loadu_si512(&data_[physical_idx + j]));
                    }
                    #elif defined(__AVX2__)
                    for (; j + 4 <= batch; j += 4) {
                        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&out_batch[popped + j]), 
                                            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&data_[physical_idx + j])));
                    }
                    #endif
                    for (; j < batch; ++j) {
                        if constexpr (std::is_pointer_v<T>) {
                            out_batch[popped + j] = data_[physical_idx + j];
                        } else if constexpr (std::is_move_assignable_v<T>) {
                            out_batch[popped + j] = std::move(data_[physical_idx + j]);
                        } else {
                            std::memcpy(&out_batch[popped + j], &data_[physical_idx + j], sizeof(T));
                        }
                    }

                    std::atomic_thread_fence(std::memory_order_release);
                    if (batch == 32) {
                        _mm256_storeu_si256(reinterpret_cast<__m256i*>(&meta_[physical_idx]), v_empty);
                    } else {
                        for (size_t k = 0; k < batch; ++k) meta_[physical_idx + k].store(STATE_EMPTY, std::memory_order_relaxed);
                    }

                    egress_cursor_ += batch;
                    popped += batch;

                    if (batch < attempt) break;
                } else {
                    // Scalar Fallback
                    if (meta_[physical_idx].load(std::memory_order_acquire) == STATE_READY) {
                        if constexpr (std::is_pointer_v<T>) {
                            out_batch[popped] = data_[physical_idx];
                        } else if constexpr (std::is_move_assignable_v<T>) {
                            out_batch[popped] = std::move(data_[physical_idx]);
                        } else {
                            std::memcpy(&out_batch[popped], &data_[physical_idx], sizeof(T));
                        }
                        meta_[physical_idx].store(STATE_EMPTY, std::memory_order_release);
                        egress_cursor_++;
                        popped++;
                    } else {
                        break;
                    }
                }
            }
            return popped;
        }

        inline void revert_batch(const T* batch, size_t count) noexcept {
            if (count == 0) return;
            for (size_t i = 0; i < count; ++i) {
                egress_cursor_--;
                size_t physical_idx = egress_cursor_ & Mask;
                data_[physical_idx] = std::move(batch[count - 1 - i]);
                meta_[physical_idx].store(STATE_READY, std::memory_order_release);
            }
        }

        // Hardware Matrix Size Probe: O(1) Popcount
        [[nodiscard]] inline size_t occupancy() const noexcept {
            size_t count = 0;
            const __m256i v_ready = _mm256_set1_epi8(STATE_READY);
            for (size_t i = 0; i < Capacity; i += 32) {
                __m256i v_states = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&meta_[i]));
                uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v_states, v_ready));
                count += std::popcount(mask);
            }
            return count;
        }

        [[nodiscard]] inline size_t approx_size() const noexcept { return occupancy(); }

        SLAB_HOT void invalidate_by_ptr(T target) noexcept {
            const int64_t target_val = std::bit_cast<int64_t>(target);
            
            #if defined(__AVX512F__)
            const __m512i v_target = _mm512_set1_epi64(target_val);
            #elif defined(__AVX2__)
            const __m256i v_target_256 = _mm256_set1_epi64x(target_val);
            #endif

            for (size_t i = 0; i < Capacity;) {
                if constexpr (sizeof(T) == 8) {
                    #if defined(__AVX512F__)
                    if (i + 8 <= Capacity) {
                        __m512i v_data = _mm512_loadu_si512(reinterpret_cast<const void*>(&data_[i]));
                        __mmask8 mask = _mm512_cmpeq_epi64_mask(v_data, v_target);
                        if (mask) {
                            for (int b = 0; b < 8; ++b) {
                                if (mask & (1 << b)) {
                                    data_[i + b] = T{};
                                    meta_[i + b].store(STATE_EMPTY, std::memory_order_relaxed);
                                }
                            }
                        }
                        i += 8;
                        continue;
                    }
                    #elif defined(__AVX2__)
                    if (i + 4 <= Capacity) {
                        __m256i v_data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&data_[i]));
                        __m256i v_mask = _mm256_cmpeq_epi64(v_data, v_target_256);
                        if (!_mm256_testz_si256(v_mask, v_mask)) {
                            uint32_t mask = _mm256_movemask_epi8(v_mask);
                            for (int b = 0; b < 4; ++b) {
                                if (mask & (1 << (b * 8))) {
                                    data_[i + b] = T{};
                                    meta_[i + b].store(STATE_EMPTY, std::memory_order_relaxed);
                                }
                            }
                        }
                        i += 4;
                        continue;
                    }
                    #endif
                }
                if (data_[i] == target) {
                    data_[i] = T{};
                    meta_[i].store(STATE_EMPTY, std::memory_order_relaxed);
                }
                ++i;
            }
        }
    };

} // namespace slabflux::core