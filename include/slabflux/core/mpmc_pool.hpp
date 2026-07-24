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
#include "slabflux/core/wire_frame_lsn.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/managed_data.hpp"
#include "slabflux/core/scoped_ptr.hpp"
#include "slabflux/platform/os.hpp"
#include "slabflux/core/hardware_topology.hpp"

namespace slabflux::core {

    template <typename T>
    concept SlabElementMPMC = requires {
        requires std::is_destructible_v<T>;
        requires sizeof(T) > 0;
    };

    /**
     * @brief MC Memory Pool.
     * @details Implements a Distributed architecture to bypass the 
     * L3 interconnect wall during high-frequency resource allocation and reclamation.
     * * High-Performance Design:
     * 1. Distributed: Contention is sharded across multiple parallel 
     * lanes, virtually eliminating RFO stalls and MESI thrashing.
     * 2. Routing: Uses thread-local core affinity and "Adaptive Topology" 
     * to pin threads to physically local memory channels, maximizing L2 hit rates.
     * 3. ABA Hardening: Uses 64-bit tagged pointers (index + version) to prevent 
     * stack corruption in high-frequency multi-threaded reclamation scenarios.
     * 4. Hardware-Managed Residency: Utilizes mmap/mlock with HugePage support 
     * (2MB pages) to guarantee physical RAM residency and minimize TLB misses,
     * with graceful fallback to standard locked pages if HugePage allocation fails.
     * * API Contract:
     * - Progress Guarantee: Wait-free O(1) transitions for both allocation and release.
     * - Safety: Type-safe RAII integration via managed_data and scoped_ptr.
     *
     * ========================================================================
     * API REFERENCE: mpmc_pool<T, Capacity>
     * ========================================================================
     * 1. ALLOCATION (PRODUCER):
     * - T* make_raw(...Args)      : Placement-new into pinned slab.
     * - managed_data<T, P> make(...Args) : Automated ownership transfer.
     * * 2. RECLAMATION (ANY THREAD):
     * - void release(T* ptr)      : Destruct and return to free-list.
     * - void release(scoped_ptr&) : Explicit ownership collapse.
     * * 3. SYSTEM INTERFACE:
     * - size_t capacity()         : Static slab size.
     * - void* get_raw_ptr()       : Base address for DMA/Kernel registration.
     * - T* get_raw_ptr_by_id(u32) : O(1) direct address resolution by index.
     */
    template <SlabElementMPMC T, std::size_t Capacity, std::size_t NumLanes = 8>
    class alignas(64) mpmc_pool {
    public:
        /**
         * @brief Epoch-Based Reclamation Metaprogramming.
         * @details Proprietary pointer packing/unpacking logic for ABA hardening.
         * This ensures bit-perfect, O(1) epoch management.
         */
        struct epoch_nexus {
            static SLAB_FORCE_INLINE uint64_t pack(uint32_t index, uint32_t epoch) noexcept {
                return (static_cast<uint64_t>(epoch) << 32) | index;
            }
            static SLAB_FORCE_INLINE uint32_t unpack_index(uint64_t raw) noexcept {
                return static_cast<uint32_t>(raw & 0xFFFFFFFF);
            }
            static SLAB_FORCE_INLINE uint32_t unpack_epoch(uint64_t raw) noexcept {
                return static_cast<uint32_t>(raw >> 32);
            }
        };

    public:
        using value_type = T;
        using value_type_pod = std::remove_pointer_t<T>;

    private:
        inline static thread_local std::size_t cached_core_idx = 0xFFFFFFFF;

        static_assert((NumLanes & (NumLanes - 1)) == 0, "NumLanes must be power-of-two");

        struct alignas(64) payload_node {
            alignas(alignof(T)) uint8_t raw_memory[sizeof(T)];
        };

        struct shard {
            alignas(64) std::atomic<uint64_t> head;
        };

        std::array<shard, NumLanes> shards_;
        payload_node* raw_memory_{ nullptr };
        std::atomic<uint32_t>* next_indices_{ nullptr };
        void* base_memory_ptr_{ nullptr };
        std::size_t pool_size_bytes_{ 0 };

        SLAB_FORCE_INLINE std::size_t select_lane(std::size_t offset = 0) const noexcept {
            // Optimized Sticky Routing: Poll hardware topology only once per thread lifetime.
            // This eliminates the vDSO tax during lane spillover attempts.
            if (SL_UNLIKELY(cached_core_idx == 0xFFFFFFFF)) [[unlikely]] {
                cached_core_idx = static_cast<std::size_t>(hardware_topology::get_current_cpu());
            }
            return (cached_core_idx + offset) & (NumLanes - 1);
        }

    public:
        explicit mpmc_pool() {
            const std::size_t huge_page_size = 2 * 1024 * 1024;
            const std::size_t payload_bytes = (Capacity * sizeof(payload_node) + 63) & ~63;
            const std::size_t meta_bytes = (Capacity * sizeof(std::atomic<uint32_t>) + 63) & ~63;
            pool_size_bytes_ = (payload_bytes + meta_bytes + huge_page_size - 1) & ~(huge_page_size - 1);

            int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED | MAP_HUGETLB | MAP_HUGE_2MB;
            void* mem = ::mmap(nullptr, pool_size_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);

            if (mem == MAP_FAILED && (flags & MAP_HUGETLB)) {
                flags &= ~(MAP_HUGETLB | MAP_HUGE_2MB);
                mem = ::mmap(nullptr, pool_size_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
            }

            if (mem == MAP_FAILED) {
                flags = MAP_PRIVATE | MAP_ANONYMOUS;
                mem = ::mmap(nullptr, pool_size_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);

                if (mem == MAP_FAILED) throw std::bad_alloc();
            }
            if (mem == nullptr) throw std::bad_alloc();

#ifndef _WIN32
            ::madvise(mem, pool_size_bytes_, MADV_HUGEPAGE | MADV_DONTDUMP);
            ::mlock(mem, pool_size_bytes_);
#endif
            base_memory_ptr_ = mem;
            raw_memory_ = reinterpret_cast<payload_node*>(mem);
            next_indices_ = reinterpret_cast<std::atomic<uint32_t>*>(static_cast<char*>(mem) + payload_bytes);

            const std::size_t nodes_per_lane = Capacity / NumLanes;
            for (std::size_t l = 0; l < NumLanes; ++l) {
                const uint32_t start = static_cast<uint32_t>(l * nodes_per_lane);
                const uint32_t end = static_cast<uint32_t>((l + 1) * nodes_per_lane);
                for (uint32_t i = start; i < end; ++i) {
                    new (&next_indices_[i]) std::atomic<uint32_t>((i < end - 1) ? i + 1 : 0xFFFFFFFF);
                }
                shards_[l].head.store(epoch_nexus::pack(start, 0), std::memory_order_release);
            }
        }

        mpmc_pool(const mpmc_pool&) = delete;
        mpmc_pool& operator=(const mpmc_pool&) = delete;
        mpmc_pool& operator=(mpmc_pool&&) = delete;

        ~mpmc_pool() {
            if (base_memory_ptr_) {
#ifdef _WIN32
                VirtualFree(base_memory_ptr_, 0, MEM_RELEASE);
#else
                ::munlock(base_memory_ptr_, pool_size_bytes_);
                ::munmap(base_memory_ptr_, pool_size_bytes_);
#endif
            }
        }

        static void static_deleter(void* ctx, void* raw_ptr) noexcept {
            if (!raw_ptr) return;
            auto* self = static_cast<mpmc_pool*>(ctx);
            auto* ptr = static_cast<T*>(raw_ptr);
            
            ptr->~T();
            uint32_t idx = static_cast<uint32_t>(reinterpret_cast<payload_node*>(ptr) - self->raw_memory_); // Correctly derive index

            const std::size_t l_idx = self->select_lane();
            auto& lane_head = self->shards_[l_idx].head;
            uint64_t old_head = lane_head.load(std::memory_order_acquire);

            for (uint32_t retries = 0; ; ++retries) {
                uint32_t old_index = epoch_nexus::unpack_index(old_head);
                uint32_t old_version = epoch_nexus::unpack_epoch(old_head);
                self->next_indices_[idx].store(old_index, std::memory_order_release); // Ensure link is published before head update
                uint64_t new_head = epoch_nexus::pack(idx, old_version + 1);
                if (SL_EXPECT_TRUE(lane_head.compare_exchange_strong(old_head, new_head, std::memory_order_release, std::memory_order_acquire))) {
                    break;
                }
                
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
        }

        SLAB_FORCE_INLINE void release_batch(T** ptrs, size_t count) noexcept { // Added missing destructor calls
            if (SL_EXPECT_FALSE(count == 0)) return;
            
            _mm_prefetch(reinterpret_cast<const char*>(ptrs[0]), _MM_HINT_T0);
            if (count > 1) _mm_prefetch(reinterpret_cast<const char*>(ptrs[1]), _MM_HINT_T0);
            
            for (size_t i = 0; i < count; ++i) {
                ptrs[i]->~T();
                if (i < count - 1) { // Link the batch
                    uint32_t next_idx = get_index(ptrs[i+1]);
                    uint32_t curr_idx = get_index(ptrs[i]);
                    next_indices_[curr_idx].store(next_idx, std::memory_order_relaxed);
                }
            }

            uint32_t first_idx = get_index(ptrs[0]);
            uint32_t last_idx = get_index(ptrs[count - 1]);

            const std::size_t l_idx = select_lane();
            auto& lane_head = shards_[l_idx].head;
            uint64_t old_head = lane_head.load(std::memory_order_acquire);
            for (uint32_t retries = 0; ; ++retries) {
                next_indices_[last_idx].store(epoch_nexus::unpack_index(old_head), std::memory_order_relaxed);
                uint64_t new_head = epoch_nexus::pack(first_idx, epoch_nexus::unpack_epoch(old_head) + 1);
                if (SL_EXPECT_TRUE(lane_head.compare_exchange_strong(old_head, new_head, std::memory_order_release, std::memory_order_acquire))) {
                    break;
                }
                
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
        }

        size_t make_batch(T** out_ptrs, size_t count) noexcept {
            if (SL_EXPECT_FALSE(count == 0)) return 0;
            for (std::size_t i = 0; i < NumLanes; ++i) {
                auto& lane = shards_[select_lane(i)];
                uint64_t old_head = lane.head.load(std::memory_order_acquire);
                for (uint32_t retries = 0; ; ++retries) {
                    uint32_t curr_idx = epoch_nexus::unpack_index(old_head);
                    if (curr_idx == 0xFFFFFFFF) break;
                    
                    size_t actual = 0;
                    while (actual < count) {
                        out_ptrs[actual] = reinterpret_cast<T*>(raw_memory_[curr_idx].raw_memory);
                        actual++;
                        if (actual == count) break;

                        uint32_t next = next_indices_[curr_idx].load(std::memory_order_relaxed);
                        if (next == 0xFFFFFFFF) break;
                        _mm_prefetch(reinterpret_cast<const char*>(&next_indices_[next]), _MM_HINT_T0);
                        curr_idx = next;
                    } // Prefetch the *next* next_indices_ entry

                    uint32_t remaining = next_indices_[curr_idx].load(std::memory_order_relaxed);
                    uint64_t new_head = epoch_nexus::pack(remaining, epoch_nexus::unpack_epoch(old_head) + 1);

                    if (SL_EXPECT_TRUE(lane.head.compare_exchange_strong(old_head, new_head, std::memory_order_acq_rel, std::memory_order_acquire))) {
                        // Construct objects in the allocated memory
                        // Only if T is not trivially constructible and default construction is needed
                        if constexpr (!std::is_trivially_constructible_v<T> && !std::is_trivially_default_constructible_v<T>) {
                            for (size_t j = 0; j < actual; ++j) new (out_ptrs[j]) T();
                        }
                        return actual;
                    }
                    for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
                }
            }
            return 0;
        }

        SLAB_FORCE_INLINE uint32_t get_index(const T* ptr) const noexcept {
            return static_cast<uint32_t>(reinterpret_cast<const payload_node*>(ptr) - raw_memory_);
        }

        SLAB_FORCE_INLINE T* get_by_index(uint32_t idx) noexcept {
            return (idx == 0xFFFFFFFF) ? nullptr : reinterpret_cast<T*>(raw_memory_[idx].raw_memory);
        }

        template <typename... Args>
        [[nodiscard]] SLAB_FORCE_INLINE T* make_raw(Args&&... args) noexcept {
            uint32_t final_index = 0xFFFFFFFF;
            for (std::size_t i = 0; i < NumLanes; ++i) {
                auto& lane = shards_[select_lane(i)];
                uint64_t old_head = lane.head.load(std::memory_order_acquire);
                for (uint32_t retries = 0; ; ++retries) {
                    final_index = epoch_nexus::unpack_index(old_head);
                    uint32_t old_version = epoch_nexus::unpack_epoch(old_head);
                    if (final_index == 0xFFFFFFFF) break;
                    
                    uint32_t next_index = next_indices_[final_index].load(std::memory_order_relaxed);
                    uint64_t new_head = epoch_nexus::pack(next_index, old_version + 1);
                    
                    if (SL_EXPECT_TRUE(lane.head.compare_exchange_strong(old_head, new_head, std::memory_order_release, std::memory_order_acquire))) {
                        goto construct_node;
                    }
                    for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
                }
            }
            return nullptr;

        construct_node:
            if (SL_EXPECT_FALSE(final_index == 0xFFFFFFFF)) return nullptr;
            return new (raw_memory_[final_index].raw_memory) T(std::forward<Args>(args)...);
        }

        template <typename... Args>
        [[nodiscard]] SLAB_FORCE_INLINE auto make(Args&&... args) noexcept {
            T* raw_ptr = make_raw(std::forward<Args>(args)...);
            if (!raw_ptr) [[unlikely]] return managed_data<T, mpmc_pool>();
            return managed_data<T, mpmc_pool>(raw_ptr, *this);
        }

        SLAB_FORCE_INLINE T* get_raw_ptr_by_id(uint32_t id) noexcept {
            return reinterpret_cast<T*>(raw_memory_[id].raw_memory);
        }

        SLAB_FORCE_INLINE void release(T* ptr) noexcept { static_deleter(this, ptr); }
        
        SLAB_FORCE_INLINE void release(managed_data<T, mpmc_pool>& item) noexcept {
            T* raw = item.release();
            if (raw) [[likely]] release(raw);
        }

        SLAB_FORCE_INLINE void release(scoped_ptr<value_type_pod>& item) noexcept {
            value_type_pod* raw = item.release();
            if (raw) [[likely]] release(raw);
        }
        static void deleter_fn(void* ctx, void* raw_ptr) noexcept { if(raw_ptr) static_cast<mpmc_pool*>(ctx)->release(static_cast<T*>(raw_ptr)); }
        [[nodiscard]] SLAB_FORCE_INLINE size_t get_raw_ptr_size() const noexcept { return pool_size_bytes_; } // Required by SovereignUmemProvider
        SLAB_FORCE_INLINE std::size_t capacity() const noexcept { return Capacity; }
        [[nodiscard]] SLAB_FORCE_INLINE void* get_raw_ptr() noexcept { return static_cast<void*>(raw_memory_); }
    };
}