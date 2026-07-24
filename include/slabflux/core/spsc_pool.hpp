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
#include <array>
#include <cstddef>
#include <new>
#include <type_traits>
#include "slabflux/core/managed_data.hpp"
#include "slabflux/core/scoped_ptr.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/platform/os.hpp"

namespace slabflux::core {

    /**
     * @brief SPSC-backed Memory Pool.
     * @details The counterpart to the spsc_conduit wire. Specialized for 1:1 
     * producer-consumer resource flows.
     * 
     * High-Performance Design:
     * - SIMD Recovery: Leverages spsc_conduit's AVX-512 invalidation to purge 
     *   polluted pointers from the free-list during journal replay.
     * - Physical Hardening: Supports HugePages and RAM pinning (mlock) to 
     *   achieve zero page-fault jitter during the hot-path.
     * - Shadow Pointers: Eliminates cross-core bus locking during standard 
     *   allocation/release cycles.
     * 
     * API Contract:
     * 1. TOPOLOGY: Strictly 1:1. Multiple threads allocating or releasing 
     *    simultaneously will cause corruption.
     * 2. ALLOCATION: Wait-free O(1). Returns nullptr if empty.
     * 
     * ========================================================================
     * API REFERENCE: spsc_pool<T, Capacity>
     * ========================================================================
     * 1. ALLOCATION (PRODUCER):
     *    - T* make_raw(...Args)      : Placement-new into slab.
     *    - managed_data<T, P> make() : Automated ownership transfer.
     * 
     * 2. RECLAMATION (CONSUMER):
     *    - void release(T* ptr)      : Return to free-list wire.
     *    - void release(scoped_ptr&) : Safe ownership collapse.
     *    - void release_batch(T**, n): Vectorized return to free-list.
     * 
     * 3. RECOVERY:
     *    - void invalidate(T* ptr)   : SIMD-accelerated pointer nulling.
     * 
     * 4. TELEMETRY & DMA:
     *    - size_t capacity()         : Static slab size.
     *    - void* get_raw_ptr()       : Base address for DMA/Kernel registration.
     *    - T* get_raw_ptr_by_id(u32) : O(1) address resolution by index.
     */
    template <typename T, std::size_t Capacity>
    class alignas(64) spsc_pool {
        using ValueType = std::remove_pointer_t<T>;
        struct alignas(64) storage_node { uint8_t raw[sizeof(T)]; };
        
        storage_node* memory_block_{ nullptr };
        std::size_t pool_size_bytes_{ 0 };
        spsc_conduit<T*, Capacity> free_ring_;

    public:
        spsc_pool() {
            pool_size_bytes_ = Capacity * sizeof(storage_node);

#ifdef _WIN32
            std::size_t large_page_size = GetLargePageMinimum();
            if (large_page_size > 0) {
                pool_size_bytes_ = (pool_size_bytes_ + large_page_size - 1) & ~(large_page_size - 1);
                memory_block_ = static_cast<storage_node*>(VirtualAlloc(
                    nullptr, pool_size_bytes_, MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES, PAGE_READWRITE));
            }
            if (!memory_block_) {
                pool_size_bytes_ = Capacity * sizeof(storage_node);
                memory_block_ = static_cast<storage_node*>(VirtualAlloc(
                    nullptr, pool_size_bytes_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            }
            if (!memory_block_) throw std::bad_alloc();
#else
            int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED | MAP_HUGETLB;
            void* mem = ::mmap(nullptr, pool_size_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);

            if (mem == MAP_FAILED) {
                flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED;
                mem = ::mmap(nullptr, pool_size_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);

                if (mem == MAP_FAILED) {
                    flags = MAP_PRIVATE | MAP_ANONYMOUS;
                    mem = ::mmap(nullptr, pool_size_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
                    if (mem == MAP_FAILED) throw std::bad_alloc();
                    ::madvise(mem, pool_size_bytes_, MADV_HUGEPAGE);
                    ::mlock(mem, pool_size_bytes_);
                }
            }
            memory_block_ = static_cast<storage_node*>(mem);
#endif

            for (uint32_t i = 0; i < Capacity; ++i) {
                free_ring_.push(reinterpret_cast<T*>(memory_block_[i].raw));
            }
        }

        ~spsc_pool() {
            if (memory_block_) {
#ifdef _WIN32
                VirtualFree(memory_block_, 0, MEM_RELEASE);
#else
                munlock(memory_block_, pool_size_bytes_);
                munmap(memory_block_, pool_size_bytes_);
#endif
            }
        }

        /**
         * @brief Static hook for automated reclamation.
         * @details Facilitates integration with RAII wrappers like managed_data and scoped_ptr.
         */
        static void deleter_fn(void* ctx, void* raw_ptr) noexcept { 
            static_cast<spsc_pool*>(ctx)->release(static_cast<T*>(raw_ptr)); 
        }

        /**
         * @brief Returns an object to the free-list.
         * @details Executes the destructor (if non-trivial) and pushes the pointer 
         * back to the SPSC free-ring.
         */
        SLAB_FORCE_INLINE void release(T* ptr) noexcept {
            if constexpr (!std::is_trivially_destructible_v<T>) ptr->~T();
            free_ring_.push(ptr);
        }

        /**
         * @brief Collective Reclamation.
         * @details Leverages the conduit's AVX-512 batching to return multiple 
         * pointers in a single instruction burst.
         */
        SLAB_FORCE_INLINE void release_batch(T** ptrs, size_t count) noexcept {
            if constexpr (!std::is_trivially_destructible_v<T>) {
                for (size_t i = 0; i < count; ++i) ptrs[i]->~T();
            }

            // Memory Topology Optimization: Warm the L1-D for the batch block
            if (count > 8) [[likely]] _mm_prefetch(reinterpret_cast<const char*>(ptrs), _MM_HINT_T0);

            size_t pushed = 0;
            while (pushed < count) {
                // Leverages AVX-512 within the conduit to move pointers in 512-bit bursts
                size_t n = free_ring_.push_batch(ptrs + pushed, count - pushed);
                if (SL_EXPECT_FALSE(n == 0)) _mm_pause();
                pushed += n;
            }
        }

        /**
         * @brief Safe ownership collapse for managed handles.
         */
        SLAB_FORCE_INLINE void release(managed_data<T, spsc_pool>& item) noexcept {
            T* raw = item.release();
            if (raw) [[likely]] release(raw);
        }

        /**
         * @brief Safe ownership collapse.
         * @details Releases the pointer from the scoped_ptr and returns it to the pool.
         */
        SLAB_FORCE_INLINE void release(scoped_ptr<ValueType>& item) noexcept {
            ValueType* raw = item.release();
            if (raw) [[likely]] release(raw);
        }

        /**
         * @brief SIMD-accelerated pointer invalidation for recovery paths.
         * @details Scans the free-list and nulls out the target pointer using AVX-512.
         * Prevents corrupted memory segments from being re-allocated after system rebirth.
         */
        SLAB_HOT void invalidate(T* ptr) noexcept { free_ring_.invalidate_by_ptr(ptr); }

        /**
         * @brief Raw Wait-Free Allocation.
         * @details Pops a pointer from the free-ring and performs placement-new.
         * @return T* or nullptr if the slab is saturated.
         */
        template <typename... Args>
        [[nodiscard]] SLAB_FORCE_INLINE T* make_raw(Args&&... args) noexcept {
            T* raw_ptr = nullptr;
            // Memory Fabric Interleaving: Pop from the SPSC ring using local cached markers to hide L3 latency.
            if (SL_EXPECT_TRUE(free_ring_.try_pop(raw_ptr))) {
                if constexpr (std::is_trivially_default_constructible_v<T> && sizeof...(Args) == 0) {
                    // Zero-Latency Path: Skip placement-new for PODs to maintain 0.6ns conduit parity.
                    return raw_ptr;
                } else {
                    return new (raw_ptr) T(std::forward<Args>(args)...);
                }
            }
            return nullptr;
        }

        /**
         * @brief Vectorized Allocation.
         * @details Grabs a batch of pointers and initializes them in a single sweep.
         * @return Number of objects successfully created.
         */
        inline size_t make_batch(T** out_ptrs, size_t count) noexcept {
            // Amortized Acquisition: One conduit trip for 'count' objects.
            size_t actual = free_ring_.pop_batch(out_ptrs, count);
            if constexpr (!std::is_trivially_default_constructible_v<T>) {
                for (size_t i = 0; i < actual; ++i) {
                    new (out_ptrs[i]) T();
                }
            }
            return actual;
        }

        /**
         * @brief Managed Allocation.
         * @return A managed_data handle for automated, thread-safe reclamation.
         */
        template <typename... Args>
        [[nodiscard]] SLAB_FORCE_INLINE auto make(Args&&... args) noexcept {
            T* raw_ptr = make_raw(std::forward<Args>(args)...);
            if (!raw_ptr) [[unlikely]] return managed_data<T, spsc_pool>();
            return managed_data<T, spsc_pool>(raw_ptr, *this);
        }

        /**
         * @brief O(1) Address resolution for hardware buffer rings.
         */
        SLAB_FORCE_INLINE T* get_raw_ptr_by_id(uint32_t id) noexcept {
            return reinterpret_cast<T*>(memory_block_[id].raw);
        }

        /** @brief Returns the statically defined object capacity. */
        SLAB_FORCE_INLINE std::size_t capacity() const noexcept { return Capacity; }

        /** @brief Returns the base address of the physically hardened memory block.
         *  @details Useful for zero-copy registration with io_uring or AF_XDP.
         */
        [[nodiscard]] SLAB_FORCE_INLINE void* get_raw_ptr() noexcept { return static_cast<void*>(memory_block_); }

        /** @brief Returns the total size of the raw memory block in bytes. */
        [[nodiscard]] SLAB_FORCE_INLINE std::size_t get_raw_ptr_size() const noexcept { return pool_size_bytes_; }
    };

} // namespace slabflux::core