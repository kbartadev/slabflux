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
#include <cstdint>
#include <cstddef>
#include <utility>
#include <new>
#include <array>
#include <type_traits>
#include <concepts>
#include <immintrin.h>
#include "slabflux/core/managed_data.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/scoped_ptr.hpp"
#include "slabflux/platform/os.hpp"
#ifndef _WIN32
#include <sys/mman.h>
#endif

namespace slabflux::core {

    enum class reclaim_strategy {
        manual,
        automatic
    };

    template <typename T>
    concept SlabElementMPSC = requires {
        requires std::is_destructible_v<T>;
        requires sizeof(T) > 0;
    };

    /**
     * @brief Asymmetric Pool: O(1) LIFO alloc with MPSC lock-free return ring.
     * @details Optimized for "Producer-Consumer" dataflows. 
     * * ========================================================================
     * API REFERENCE: mpsc_pool<T, Capacity, Strategy>
     * ========================================================================
     * 1. ALLOCATION:
     * - T* make_raw(...Args)      : O(1) stack allocation.
     * - managed_data make(...)     : Ownership-integrated allocation.
     * 2. RECLAMATION:
     * - void release(T* ptr)      : Push to MPSC return ring.
     * - void release(scoped_ptr&): Safe ownership collapse.
     * - void reclaim_returns()    : Producer-side sweep of the return ring.
     * - deleter_fn                : Static hook for scoped_ptr integration.
     * * 3. TELEMETRY & DMA:
     * - size_t capacity()         : Returns slab capacity.
     * - T* get_raw_ptr_by_id(u32) : O(1) direct address resolution by index.
     */
    template <SlabElementMPSC T, std::size_t Capacity, reclaim_strategy Strategy = reclaim_strategy::manual>
    class alignas(64) mpsc_pool {
        using ValueType = std::remove_pointer_t<T>;

        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2 for masking");
        static constexpr std::size_t Mask = Capacity - 1;

        struct storage_node { alignas(64) uint8_t raw[sizeof(T)]; };

        // --- Fast-path LIFO stack (Thread-Local to Producer) ---
        storage_node* storage_{ nullptr };
        T** lifo_stack_{ nullptr };
        alignas(64) std::size_t lifo_head_ = Capacity;

        // --- Slow-path MPSC Return Ring (Cross-Thread) ---
        // Detached Matrix: Metadata and Pointers are separated for SIMD efficiency.
        std::atomic<std::size_t>* return_sequences_{ nullptr };
        T** return_ptrs_{ nullptr }; // This is already T**, no atomic needed here directly

        alignas(64) std::atomic<std::size_t> return_push_{0};
        alignas(64) std::atomic<std::size_t> return_pop_{0};
        static constexpr size_t MAX_RECLAIM_BURST = 1024;
        std::size_t total_bytes_{ 0 };

    public:
        mpsc_pool() {
            const size_t storage_bytes = (Capacity * sizeof(storage_node) + 4095) & ~4095;
            const size_t stack_bytes = (Capacity * sizeof(T*) + 4095) & ~4095;
            const size_t seq_bytes = (Capacity * sizeof(std::atomic<std::size_t>) + 4095) & ~4095;
            const size_t ret_ptr_bytes = (Capacity * sizeof(T*) + 4095) & ~4095;
            total_bytes_ = storage_bytes + stack_bytes + seq_bytes + ret_ptr_bytes;

            #ifndef _WIN32
            int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED | MAP_HUGETLB | MAP_HUGE_2MB;
            void* mem = ::mmap(nullptr, total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
            if (mem == MAP_FAILED) {
                flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED;
                mem = ::mmap(nullptr, total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
                // If physical pinning (mlock) is denied by the OS (ulimit -l),
                // fall back to standard demand-paged memory to prevent initialization failure.
                if (mem == MAP_FAILED) {
                    flags = MAP_PRIVATE | MAP_ANONYMOUS;
                    mem = ::mmap(nullptr, total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
                }
            }
            if (mem == MAP_FAILED || mem == nullptr) throw std::bad_alloc();
            ::madvise(mem, total_bytes_, MADV_HUGEPAGE);
            #else
            void* mem = ::VirtualAlloc(nullptr, total_bytes_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!mem) throw std::bad_alloc();
            #endif

            storage_ = static_cast<storage_node*>(mem);
            lifo_stack_ = reinterpret_cast<T**>(static_cast<char*>(mem) + storage_bytes);
            return_sequences_ = reinterpret_cast<std::atomic<std::size_t>*>(static_cast<char*>(mem) + storage_bytes + stack_bytes);
            return_ptrs_ = reinterpret_cast<T**>(static_cast<char*>(mem) + storage_bytes + stack_bytes + seq_bytes);

            for (std::size_t i = 0; i < Capacity; ++i) {
                lifo_stack_[i] = reinterpret_cast<T*>(storage_[i].raw);
                // Initialize each atomic sequence number directly
                // This ensures the initial state for the sequence numbers is correct
                return_sequences_[i].store(i, std::memory_order_relaxed);
            }
        }

        ~mpsc_pool() {
            if (storage_) {
                #ifndef _WIN32
                ::munmap(storage_, total_bytes_);
                #else
                ::VirtualFree(storage_, 0, MEM_RELEASE);
                #endif
            }
        }

        template<typename... Args>
        SLAB_FORCE_INLINE T* make_raw(Args&&... args) noexcept {
            if (lifo_head_ == 0) {
                if constexpr (Strategy == reclaim_strategy::automatic) reclaim_returns();
                if (lifo_head_ == 0) [[unlikely]] return nullptr;
            }
            T* ptr = lifo_stack_[--lifo_head_];
            
            // Hardware Pipelining: Prime the payload areas of the next two LIFO slots to eliminate L1-D misses.
            if (lifo_head_ > 0) __builtin_prefetch(lifo_stack_[lifo_head_ - 1], 1, 3);
            if (lifo_head_ > 1) __builtin_prefetch(lifo_stack_[lifo_head_ - 2], 0, 1);
            
            return new (ptr) T(std::forward<Args>(args)...);
        }
        
        /**
         * @brief Collective Acquisition (Batch Allocation).
         * @details Reaps maximum benefit from the LIFO stack's contiguous layout.
         */
        [[nodiscard]] SLAB_FORCE_INLINE size_t make_batch(T** out_ptrs, size_t count) noexcept {
            if (lifo_head_ < count) {
                if constexpr (Strategy == reclaim_strategy::automatic) reclaim_returns();
            }
            size_t allocated = 0;
            while (allocated < count && lifo_head_ > 0) {
                out_ptrs[allocated] = lifo_stack_[--lifo_head_];
                if constexpr (!std::is_trivially_default_constructible_v<T>) {
                    new (out_ptrs[allocated]) T();
                }
                allocated++;
            }
            return allocated;
        }

        /**
         * @brief Managed Allocation (Producer-side only).
         */
        template <typename... Args>
        [[nodiscard]] SLAB_FORCE_INLINE auto make(Args&&... args) noexcept {
            T* raw_ptr = make_raw(std::forward<Args>(args)...);
            if (!raw_ptr) [[unlikely]] return managed_data<T, mpsc_pool>();
            return managed_data<T, mpsc_pool>(raw_ptr, *this);
        }

        /**
         * @brief Direct address resolution for DMA indexing.
         */
        SLAB_FORCE_INLINE T* get_raw_ptr_by_id(uint32_t id) noexcept {
            return reinterpret_cast<T*>(storage_[id].raw);
        }

        SLAB_FORCE_INLINE void release(T* ptr) noexcept {
            if constexpr (!std::is_trivially_destructible_v<T>) ptr->~T();
            
            // Optimization: Relaxed ordering is sufficient for ticket acquisition.
            // The store-release on return_sequences_ handles the publishing barrier.
            std::size_t pos = return_push_.fetch_add(1, std::memory_order_relaxed);
            const std::size_t idx = pos & Mask;

            // Corrected wait primitive: block while the slot is not yet recycled by the consumer.
            std::size_t seq;
            while ((seq = return_sequences_[idx].load(std::memory_order_acquire)) != pos) {
                return_sequences_[idx].wait(seq, std::memory_order_acquire);
            }
            
            return_ptrs_[idx] = ptr;
            return_sequences_[idx].store(pos + 1, std::memory_order_release);
            return_sequences_[idx].notify_one();
        }

        /**
         * @brief Collective Reclamation.
         * @details Amortizes atomic overhead by returning a batch of pointers 
         * using a single synchronization point.
         */
        SLAB_FORCE_INLINE void release_batch(T** ptrs, size_t count) noexcept {
            if constexpr (!std::is_trivially_destructible_v<T>) {
                for (size_t i = 0; i < count; ++i) ptrs[i]->~T();
            }

            size_t start_pos = return_push_.fetch_add(count, std::memory_order_relaxed);
            for (size_t i = 0; i < count; ++i) {
                const size_t pos = start_pos + i;
                const std::size_t idx = pos & Mask;
                
                // Use the atomic element directly
                while (return_sequences_[idx].load(std::memory_order_acquire) != pos) { _mm_pause(); }
                
                return_ptrs_[idx] = ptrs[i];
                return_sequences_[idx].store(pos + 1, std::memory_order_release);
                return_sequences_[idx].notify_one();
            }
        }

        /** @brief Collapses ownership and returns memory to the MPSC ring. */
        SLAB_FORCE_INLINE void release(managed_data<T, mpsc_pool>& item) noexcept {
            T* raw = item.release();
            if (raw) [[likely]] release(raw);
        }

        /** @brief Collapses ownership and returns memory to the MPSC ring. */
        SLAB_FORCE_INLINE void release(scoped_ptr<ValueType>& item) noexcept {
            ValueType* raw = item.release();
            if (raw) [[likely]] release(raw);
        }

        /** * @brief Producer-side sweep of the return ring.
         * @details Amortized to prevent large bursts from causing latency spikes.
         */
        SLAB_FORCE_INLINE void reclaim_returns() noexcept {
            auto p = return_pop_.load(std::memory_order_relaxed);
            auto push_idx = return_push_.load(std::memory_order_acquire);
            size_t burst = 0;
            // The `while (p < push_idx && burst < MAX_RECLAIM_BURST)` condition is more accurate for loop control.
            while (p < push_idx && burst < MAX_RECLAIM_BURST) {
                const std::size_t idx = p & Mask;
                // Use the atomic element directly
                // Topology Warming: Prefetch the next sequence and pointer data for the *next* iteration.
                _mm_prefetch(reinterpret_cast<const char*>(&return_sequences_[(p + 1) & Mask]), _MM_HINT_T0);
                _mm_prefetch(reinterpret_cast<const char*>(&return_ptrs_[(p + 1) & Mask]), _MM_HINT_T0);

                std::size_t seq = return_sequences_[idx].load(std::memory_order_acquire);
                if (seq != p + 1) break; // Gap or empty
                
                lifo_stack_[lifo_head_++] = return_ptrs_[idx];
                return_sequences_[idx].store(p + Capacity, std::memory_order_release);
                return_sequences_[idx].notify_one();
                p++;
                burst++;
            }
            return_pop_.store(p, std::memory_order_relaxed);
        }

        static void deleter_fn(void* ctx, void* raw_ptr) noexcept { 
            if (raw_ptr) [[likely]] static_cast<mpsc_pool*>(ctx)->release(static_cast<T*>(raw_ptr)); 
        }
        SLAB_FORCE_INLINE std::size_t capacity() const noexcept { return Capacity; }
    };

    // Backward compatibility alias for the V1 Asymmetric naming convention
    template <typename T, std::size_t Capacity, reclaim_strategy Strategy = reclaim_strategy::manual>
    using asymmetric_pool = mpsc_pool<T, Capacity, Strategy>;

}