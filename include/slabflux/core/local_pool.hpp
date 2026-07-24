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

 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 * Absolute Liability Limitation & Full Terms: See LICENSE.
 *
 * @file local_pool.hpp
 * @brief eaded Memory Pool.
 * @details Optimized for zero-atomic execution paths.
 */

#pragma once
#include "mpmc_pool.hpp"
#include "slabflux/core/managed_data.hpp"
#include "slabflux/core/scoped_ptr.hpp"
#ifndef _WIN32
#include <sys/mman.h>
#endif

namespace slabflux::core {

    /**
     * @brief eaded Memory Pool.
     * @details Optimized for zero-atomic execution paths. Implements a LIFO 
     * allocator with no synchronization overhead, ideal for thread-confined 
     * buffers or isolated execution units.
     * 
     * High-Performance Design:
     * 1. 0ns Synchronization: Eliminates all atomic barriers and CAS loops.
     * 2. Physical Residency: Memory is allocated via 2MB HugePages and 
     *    pinned (mlock) to achieve O(1) page access.
     */
    template <typename T, std::size_t Capacity>
    class alignas(64) local_pool {
    public:
        using value_type = T;
        using value_type_pod = std::remove_pointer_t<T>;

    private:
        struct alignas(64) payload_node {
            alignas(alignof(T)) uint8_t raw_memory[sizeof(T)];
        };

        uint32_t head_{ 0xFFFFFFFF };
        payload_node* raw_memory_{ nullptr };
        uint32_t* next_indices_{ nullptr };
        void* base_memory_ptr_{ nullptr };
        std::size_t pool_size_bytes_{ 0 };

    public:
        explicit local_pool() {
            const std::size_t huge_page_size = 2 * 1024 * 1024;
            const std::size_t payload_bytes = (Capacity * sizeof(payload_node) + 63) & ~63;
            const std::size_t meta_bytes = (Capacity * sizeof(uint32_t) + 63) & ~63;
            pool_size_bytes_ = (payload_bytes + meta_bytes + huge_page_size - 1) & ~(huge_page_size - 1);

            #ifndef _WIN32
            int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED | MAP_HUGETLB | MAP_HUGE_2MB;
            void* mem = ::mmap(nullptr, pool_size_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);

            if (mem == MAP_FAILED) {
                flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED;
                mem = ::mmap(nullptr, pool_size_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
            }

            if (mem == MAP_FAILED) {
                flags = MAP_PRIVATE | MAP_ANONYMOUS;
                mem = ::mmap(nullptr, pool_size_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
                if (mem == MAP_FAILED) throw std::bad_alloc();
            }
            ::madvise(mem, pool_size_bytes_, MADV_HUGEPAGE | MADV_DONTDUMP);
            ::mlock(mem, pool_size_bytes_);
            #else
            void* mem = ::VirtualAlloc(nullptr, pool_size_bytes_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!mem) throw std::bad_alloc();
            #endif

            base_memory_ptr_ = mem;
            raw_memory_ = reinterpret_cast<payload_node*>(mem);
            next_indices_ = reinterpret_cast<uint32_t*>(static_cast<char*>(mem) + payload_bytes);

            for (uint32_t i = 0; i < Capacity; ++i) {
                next_indices_[i] = (i < Capacity - 1) ? i + 1 : 0xFFFFFFFF;
            }
            head_ = 0;
        }

        ~local_pool() {
            if (base_memory_ptr_) {
                #ifdef _WIN32
                VirtualFree(base_memory_ptr_, 0, MEM_RELEASE);
                #else
                ::munlock(base_memory_ptr_, pool_size_bytes_);
                ::munmap(base_memory_ptr_, pool_size_bytes_);
                #endif
            }
        }

        local_pool(const local_pool&) = delete;
        local_pool& operator=(const local_pool&) = delete;

        static void static_deleter(void* ctx, void* raw_ptr) noexcept {
            if (!raw_ptr) return;
            auto* self = static_cast<local_pool*>(ctx);
            auto* ptr = static_cast<T*>(raw_ptr);
            ptr->~T();
            uint32_t idx = self->get_index(ptr);
            self->next_indices_[idx] = self->head_;
            self->head_ = idx;
        }

        SLAB_FORCE_INLINE void release(T* ptr) noexcept { static_deleter(this, ptr); }

        SLAB_FORCE_INLINE uint32_t get_index(const T* ptr) const noexcept {
            return static_cast<uint32_t>(reinterpret_cast<const payload_node*>(ptr) - raw_memory_);
        }

        SLAB_FORCE_INLINE T* get_by_index(uint32_t id) noexcept {
            return (id == 0xFFFFFFFF) ? nullptr : reinterpret_cast<T*>(raw_memory_[id].raw_memory);
        }

        /** @brief Collective Reclamation: Links a batch of pointers to the free-list. */
        SLAB_FORCE_INLINE void release_batch(T** ptrs, size_t count) noexcept {
            if (SL_EXPECT_FALSE(count == 0)) return;
            for (size_t i = 0; i < count; ++i) {
                ptrs[i]->~T();
                if (i < count - 1) {
                    next_indices_[get_index(ptrs[i])] = get_index(ptrs[i+1]);
                }
            }
            next_indices_[get_index(ptrs[count - 1])] = head_;
            head_ = get_index(ptrs[0]);
        }

        /** @brief Batch Allocation: Pulls a chain of nodes in a single cycle. */
        size_t make_batch(T** out_ptrs, size_t count) noexcept {
            size_t actual = 0;
            while (actual < count && head_ != 0xFFFFFFFF) {
                uint32_t idx = head_;
                head_ = next_indices_[idx];
                T* ptr = reinterpret_cast<T*>(raw_memory_[idx].raw_memory);
                if constexpr (!std::is_trivially_default_constructible_v<T>) {
                    new (ptr) T();
                }
                out_ptrs[actual++] = ptr;
            }
            return actual;
        }

        /** @brief Placement-new into physically pinned RAM. */
        template <typename... Args>
        [[nodiscard]] SLAB_FORCE_INLINE T* make_raw(Args&&... args) noexcept {
            if (SL_EXPECT_FALSE(head_ == 0xFFFFFFFF)) return nullptr;
            uint32_t idx = head_;
            head_ = next_indices_[idx];
            return new (raw_memory_[idx].raw_memory) T(std::forward<Args>(args)...);
        }

        /** @brief RAII Allocation with automatic reclamation. */
        template <typename... Args>
        [[nodiscard]] SLAB_FORCE_INLINE auto make(Args&&... args) noexcept {
            T* raw_ptr = make_raw(std::forward<Args>(args)...);
            if (!raw_ptr) [[unlikely]] return managed_data<T, local_pool>();
            return managed_data<T, local_pool>(raw_ptr, *this);
        }

        /** @brief Ownership transfer from managed_data handle. */
        SLAB_FORCE_INLINE void release(managed_data<T, local_pool>& item) noexcept {
            T* raw = item.release();
            if (raw) [[likely]] release(raw);
        }

        /** @brief Ownership transfer from scoped_ptr handle. */
        SLAB_FORCE_INLINE void release(scoped_ptr<value_type_pod>& item) noexcept {
            value_type_pod* raw = item.release();
            if (raw) [[likely]] release(raw);
        }

        /** @brief O(1) pointer resolution for zero-copy DMA. */
        SLAB_FORCE_INLINE T* get_raw_ptr_by_id(uint32_t id) noexcept {
            return reinterpret_cast<T*>(raw_memory_[id].raw_memory);
        }

        static void deleter_fn(void* ctx, void* raw_ptr) noexcept {
            if(raw_ptr) static_cast<local_pool*>(ctx)->release(static_cast<T*>(raw_ptr));
        }

        SLAB_FORCE_INLINE std::size_t capacity() const noexcept { return Capacity; }
        [[nodiscard]] SLAB_FORCE_INLINE void* get_raw_ptr() noexcept { return static_cast<void*>(raw_memory_); }
    };
}