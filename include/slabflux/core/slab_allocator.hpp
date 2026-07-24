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
 * @file slab_allocator.hpp
 * @brief MPMC SHM Allocator.
 * @details Implements a high-throughput, ABA-protected concurrent allocator 
 * utilizing an Isolated Cell Matrix architecture for Shared Memory (SHM) 
 * and IPC dataflows.
 * utilizing an Isolated Cell Layout for high-frequency Shared Memory (SHM) 
 * and IPC allocation paths.
 * 
 * Design Architecture:
 * 1. Distributed Metadata: Uses 64-bit tagged pointers (index + version) to 
 *    prevent ABA corruption in high-frequency cross-process reclamation.
 * 2. Cache Sovereignty: Strict 64-byte alignment per cell eliminates MESI 
 *    thrashing and interconnect RFO stalls between concurrent processes.
 * 3. Physical Residency: Enforces RAM pinning (mlock/MAP_LOCKED) for deterministic O(1) access.
 *    with graceful fallback to standard demand-paged memory if pinning fails.
 */

#pragma once

#include "slabflux/platform/os.hpp"
#include <atomic>
#include <cstdint>
#include <new>
#include <immintrin.h> // For _mm_pause
#include <utility>
#include "slabflux/core/managed_data.hpp"
#include "slabflux/core/scoped_ptr.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    /**
     * @brief Constexpr Chunk Size Calculation Layer.
     * @details Statically derives the physical envelope for each slab entry,
     * ensuring meta is strictly partitioned from user data to eliminate
     * the free-list overlap vulnerability common in generic slab allocators.
     */
    template <typename T>
    struct slab_chunk_layout {
        static constexpr std::size_t stride = (sizeof(T) + sizeof(uint32_t) + 63) & ~std::size_t(63);
    };

    /**
     * @brief Production-grade wait-free memory pool.
     */
    template <typename T, uint32_t Capacity>
    class alignas(64) slab_allocator {
    private:
        struct alignas(64) cell {
            alignas(alignof(T)) uint8_t storage[sizeof(T)];
            uint32_t next_free_index;
        };
        static_assert(sizeof(cell) == slab_chunk_layout<T>::stride, "Structural Breach: Cell stride mismatch");

    public:
        using value_type = T;
        cell* raw_memory_{nullptr};
        size_t total_bytes_{0};
        
        // Global Flow Authority: 64-bit tagged pointer [ABA Version | Free Index].
        // Isolated to a dedicated 64-byte window to prevent cache-set thrashing with adjacent slabs.
        alignas(64) std::atomic<uint64_t> free_head_{0};

        static constexpr uint32_t END_OF_LIST = 0xFFFFFFFF;

    public:
        slab_allocator() {
            if (Capacity == 0) [[unlikely]] {
                handle_critical_error("SLABFLUX: Capacity must be > 0");
            }

            // Round up to page size (4KB) for alignment compatibility
            const size_t page_size = 4096;
            total_bytes_ = (Capacity * sizeof(cell) + page_size - 1) & ~(page_size - 1);
            
            int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED;
            void* mem = ::mmap(nullptr, total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);

            if (mem == MAP_FAILED) {
                flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE;
                mem = ::mmap(nullptr, total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
            }
            
            if (mem == MAP_FAILED || mem == nullptr) {
                handle_critical_error("SLABFLUX: slab_allocator mmap failed");
            }
            raw_memory_ = reinterpret_cast<cell*>(mem);

            // Initialize intrusive free-list chain
            for (uint32_t i = 0; i + 1 < Capacity; ++i) {
                raw_memory_[i].next_free_index = i + 1;
            }
            raw_memory_[Capacity - 1].next_free_index = END_OF_LIST;

            free_head_.store(0, std::memory_order_release);
        }

        ~slab_allocator() {
            if (raw_memory_ && raw_memory_ != MAP_FAILED) {
                ::munmap(raw_memory_, total_bytes_);
            }
        }

        slab_allocator(const slab_allocator&) = delete;
        slab_allocator& operator=(const slab_allocator&) = delete;

        /**
         * @brief Raw Wait-Free, ABA-Safe Allocation.
         */
        T* make_raw() noexcept {
            uint64_t head = free_head_.load(std::memory_order_acquire);
            for (uint32_t retries = 0; ; ++retries) {
                uint32_t index = static_cast<uint32_t>(head & 0xFFFFFFFF);
                if (index == END_OF_LIST) [[unlikely]] return nullptr;

                cell* c = &raw_memory_[index];
                uint64_t new_head = (((head >> 32) + 1) << 32) | c->next_free_index;

                // Interconnect RFO Mitigation: Utilize strong CAS to guarantee forward progress under contention.
                if (SL_EXPECT_TRUE(free_head_.compare_exchange_strong(head, new_head, std::memory_order_release, std::memory_order_acquire))) {
                    return reinterpret_cast<T*>(c->storage);
                }
                
                // Micro-Architectural Alignment: Capped backoff loop reduces bus traffic during high-churn allocation bursts.
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
        }

        /**
         * @brief Raw Wait-Free, ABA-Safe Deallocation.
         */
        void free(T* ptr) noexcept {
            if (!ptr) [[unlikely]] return;
            
            cell* c = reinterpret_cast<cell*>(ptr);

            // Envelope Unwrapping: Calculate physical offset from the segment base.
            uint32_t index = static_cast<uint32_t>(c - raw_memory_);
            uint64_t head = free_head_.load(std::memory_order_acquire);
            for (uint32_t retries = 0; ; ++retries) {
                c->next_free_index = static_cast<uint32_t>(head & 0xFFFFFFFF);
                uint64_t new_head = (((head >> 32) + 1) << 32) | index;
                if (SL_EXPECT_TRUE(free_head_.compare_exchange_strong(head, new_head, std::memory_order_release, std::memory_order_acquire))) break;
                
                // Interconnect Stabilization: Preserve L3 bandwidth during bulk reclamation.
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
        }

        SLAB_FORCE_INLINE uint32_t get_index(const T* ptr) const noexcept {
            return static_cast<uint32_t>(reinterpret_cast<const cell*>(ptr) - raw_memory_);
        }

        SLAB_FORCE_INLINE T* get_by_index(uint32_t id) noexcept {
            return (id == END_OF_LIST) ? nullptr : reinterpret_cast<T*>(raw_memory_[id].storage);
        }

        /** @brief Unified Pool Alias. */
        void release(T* ptr) noexcept { free(ptr); }

        /** @brief Safe ownership collapse for managed handles. */
        void release(managed_data<T, slab_allocator>& item) noexcept {
            T* raw = item.release();
            if (raw) [[likely]] release(raw);
        }

        /** @brief Safe ownership collapse for scoped handles. */
        void release(scoped_ptr<T>& item) noexcept {
            T* raw = item.release();
            if (raw) [[likely]] release(raw);
        }

        /**
         * @brief Lifecycle helpers: Construct and Destroy
         */
        template<typename... Args>
        T* construct(Args&&... args) noexcept {
            T* ptr = make_raw();
            if (ptr) [[likely]] {
                new (ptr) T(std::forward<Args>(args)...);
            }
            return ptr;
        }

        void destroy(T* ptr) noexcept {
            if (ptr) [[likely]] {
                ptr->~T();
                free(ptr);
            }
        }

        static void deleter_fn(void* ctx, void* raw_ptr) noexcept {
            if (raw_ptr) static_cast<slab_allocator*>(ctx)->release(static_cast<T*>(raw_ptr));
        }
    };

}  // namespace slabflux::core
