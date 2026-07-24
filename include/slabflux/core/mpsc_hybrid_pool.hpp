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

#include <immintrin.h> // For _mm_pause
#include "slabflux/core/wire_frame_lsn.hpp" // For T if it's a wire_frame_lsn
#include "slabflux/platform/os.hpp"
#include "mpmc_pool.hpp"
#include "mpsc_pool.hpp"

namespace slabflux::core {

    /** @brief Concept enforcement to match the MC/SC standards. */
    template <typename T>
    concept SlabElementHybrid = requires {
        requires std::is_destructible_v<T>;
        requires sizeof(T) > 0;
    };

    class os_memory_provider { // This class is correctly placed here.
    protected:
        void* raw_ptr_ = nullptr;
        std::size_t size_bytes_ = 0;

        /** @brief Physical Allocation. */
        void allocate(std::size_t size, bool strict_hugepages = true) {
            // Round up to 2MB boundary for HugePage compatibility
            const std::size_t huge_page_size = 2 * 1024 * 1024;
            size_bytes_ = (size + huge_page_size - 1) & ~(huge_page_size - 1);

    #ifdef _WIN32
            raw_ptr_ = VirtualAlloc(nullptr, size_bytes_, MEM_COMMIT | MEM_RESERVE | MEM_LARGE_PAGES, PAGE_READWRITE);
            if (!raw_ptr_ && !strict_hugepages) {
                raw_ptr_ = VirtualAlloc(nullptr, size_bytes_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            }
    #else
            int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_HUGE_2MB | MAP_LOCKED | MAP_POPULATE;
            raw_ptr_ = ::mmap(nullptr, size_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);

            if (raw_ptr_ == MAP_FAILED && !strict_hugepages) {
                flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_LOCKED | MAP_POPULATE;
                raw_ptr_ = ::mmap(nullptr, size_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);

                if (raw_ptr_ == MAP_FAILED) {
                    // Permissive Fallback: Allow standard demand-paged memory if locked RAM limits are exceeded.
                    flags = MAP_PRIVATE | MAP_ANONYMOUS;
                    raw_ptr_ = ::mmap(nullptr, size_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
                }
            }

            if (raw_ptr_ != MAP_FAILED && raw_ptr_ != nullptr) {
                ::madvise(raw_ptr_, size_bytes_, MADV_HUGEPAGE | MADV_DONTDUMP);
            }
    #endif
            if (raw_ptr_ == MAP_FAILED || raw_ptr_ == nullptr) throw std::bad_alloc();
        }

    public:
        ~os_memory_provider() {
            if (raw_ptr_) {
    #ifdef _WIN32
                VirtualFree(raw_ptr_, 0, MEM_RELEASE);
    #else
                ::munlock(raw_ptr_, size_bytes_); // Qualified
                ::munmap(raw_ptr_, size_bytes_); // Qualified
    #endif
            }
        }
    };

    /**
     * @brief C Pool.
     * @details A specialized memory pool utilizing a hybrid reclamation strategy 
     * that merges explicit OS memory control (HugePages/mlock) with an 
     * Asymmetric MPSC return architecture. Optimized for cross-process (SHM) 
     * and cross-core high-frequency resource management.
     * 
     * High-Performance Design:
     * 1. Asymmetric Egress: Uses a lock-free return ring to allow cross-thread 
     *    reclamation without polluting the producer's L1-D cache or causing RFO stalls.
     * 2. Distributed Metadata: Shards state between atomic markers and intrusive 
     *    next-links to maximize parallel instruction throughput.
     * 3. Physical Residency: Forces the OS to map memory to physical RAM 
     *    immediately (MAP_POPULATE) and prevents swapping (MAP_LOCKED).
     * 4. TLB Optimization: Defaults to 2MB HugePages to minimize TLB 
     *    misses during large state-block traversals.
     * 
     * API Contract:
     * 1. ALLOCATION: Deterministic O(1). Supports shared (atomic) and local modes.
     * 2. RECLAMATION: Asymmetric. Supports automatic or manual ring reclamation.
     *
     * ========================================================================
     * API REFERENCE: mpsc_hybrid_pool<T, Capacity, Mode, Strategy>
     * ========================================================================
     * 1. ALLOCATION:
     *    - T* make_raw(...Args)      : Placement-new into pinned physical RAM.
     *    - managed_data<T, P> make(...Args) : Automated ownership transfer.
     * 
     * 2. RECLAMATION:
     *    - void release(T* ptr)      : Destruct and return via MPSC ring.
     * 
     * 3. TELEMETRY & DMA:
     *    - size_t capacity()         : Slab size.
     *    - T* get_raw_ptr_by_id(u32) : Direct address resolution.
     */
    template <SlabElementHybrid T, std::size_t Capacity, reclaim_strategy Strategy = reclaim_strategy::manual>
    class alignas(64) mpsc_hybrid_pool : private os_memory_provider {
    public:
        using value_type = T;
        using value_type_pod = std::remove_pointer_t<T>;
        
    private:
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");

        struct node {
            alignas(std::hardware_constructive_interference_size) T data;
            uint32_t next;
        };

        node* nodes_;
        alignas(std::hardware_constructive_interference_size) std::atomic<uint64_t> head_{0};
        
        // Wait-free intrusive MPSC return stack to prevent capacity overwrites
        alignas(64) std::atomic<uint64_t> return_head_{0xFFFFFFFF};

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
        mpsc_hybrid_pool() {
            this->allocate(Capacity * sizeof(node), false /* allow fallback */);
            nodes_ = static_cast<node*>(raw_ptr_);
            for(std::size_t i = 0; i < Capacity - 1; ++i) {
                nodes_[i].next = static_cast<uint32_t>(i + 1);
            }
            nodes_[Capacity - 1].next = 0xFFFFFFFF;
            head_.store(epoch_nexus::pack(0, 0), std::memory_order_release);
        }

        template<typename... Args>
        T* make_raw(Args&&... args) noexcept {
            uint32_t idx; // Will store the index of the allocated node
            uint64_t old_head = head_.load(std::memory_order_acquire); // Load current head with acquire semantics
            for (uint32_t retries = 0; ; ++retries) {
                idx = epoch_nexus::unpack_index(old_head);
                if (idx == 0xFFFFFFFF) {
                    if constexpr (Strategy == reclaim_strategy::automatic) {
                        reclaim_returns();
                        old_head = head_.load(std::memory_order_acquire); // Reload head after reclamation attempt
                        idx = epoch_nexus::unpack_index(old_head);
                    }
                    if (idx == 0xFFFFFFFF) return nullptr;
                }

                // Performance: Raw read is safe here; atomicity is provided by the head CAS
                uint64_t new_head = epoch_nexus::pack(nodes_[idx].next, 
                                                          epoch_nexus::unpack_epoch(old_head) + 1);
                // Attempt to update the head pointer
                if (head_.compare_exchange_strong(old_head, new_head, 
                    std::memory_order_release, std::memory_order_acquire)) {
                    // Successful allocation removes items; no notification needed as 
                    // allocation doesn't unblock availability waiters.
                    break;
                }
                
                // Interconnect RFO Mitigation: Capped exponential backoff to preserve L3 memory fabric bandwidth.
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
            
            return new (&nodes_[idx].data) T(std::forward<Args>(args)...);
        }

        /** @brief Batch Acquisition. Reduces synchronization tax by grabbing a chain in 1 transaction. */
        size_t make_batch(T** out_ptrs, size_t count) noexcept {
            if (SL_EXPECT_FALSE(count == 0)) return 0;
            size_t actual = 0;

            uint64_t old_head = head_.load(std::memory_order_acquire);
            for (uint32_t retries = 0; ; ++retries) {
                uint32_t curr_idx = epoch_nexus::unpack_index(old_head);
                if (curr_idx == 0xFFFFFFFF) {
                    if constexpr (Strategy == reclaim_strategy::automatic) {
                        reclaim_returns();
                        old_head = head_.load(std::memory_order_acquire);
                        curr_idx = epoch_nexus::unpack_index(old_head);
                    }
                    if (curr_idx == 0xFFFFFFFF) return 0;
                }

                actual = 0;
                uint32_t walk_idx = curr_idx;
                while (actual < count && walk_idx != 0xFFFFFFFF) {
                    out_ptrs[actual++] = &nodes_[walk_idx].data;
                    // Prefetch the *next* node in the chain to hide memory latency
                    const uint32_t next_ptr_idx = nodes_[walk_idx].next;
                    if (next_ptr_idx != 0xFFFFFFFF) [[likely]] {
                        _mm_prefetch(reinterpret_cast<const char*>(&nodes_[next_ptr_idx]), _MM_HINT_T0);
                    }
                    walk_idx = nodes_[walk_idx].next; // chain-walk
                }

                uint64_t new_head = epoch_nexus::pack(walk_idx, epoch_nexus::unpack_epoch(old_head) + 1);
                if (head_.compare_exchange_strong(old_head, new_head, 
                                                 std::memory_order_release, std::memory_order_acquire)) {
                    if constexpr (!std::is_trivially_default_constructible_v<T>) {
                        for (size_t j = 0; j < actual; ++j) new (out_ptrs[j]) T();
                    }
                    return actual;
                }
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
            return actual;
        }

        SLAB_FORCE_INLINE uint32_t get_index(const T* ptr) const noexcept {
            return static_cast<uint32_t>(reinterpret_cast<const node*>(ptr) - nodes_);
        }

        SLAB_FORCE_INLINE T* get_by_index(uint32_t id) noexcept {
            return (id == 0xFFFFFFFF) ? nullptr : &nodes_[id].data;
        }

        /**
         * @brief Managed Allocation.
         * @return A managed_data handle utilizing hybrid reclamation.
         */
        template <typename... Args>
        [[nodiscard]] SLAB_FORCE_INLINE auto make(Args&&... args) noexcept {
            T* raw_ptr = make_raw(std::forward<Args>(args)...);
            if (!raw_ptr) [[unlikely]] return managed_data<T, mpsc_hybrid_pool>();
            return managed_data<T, mpsc_hybrid_pool>(raw_ptr, *this);
        }

        /** @brief O(1) physical address resolution. */
        SLAB_FORCE_INLINE T* get_raw_ptr_by_id(uint32_t id) noexcept {
            return &nodes_[id].data;
        }

        /** @brief Returns the base address for DMA registration. */
        [[nodiscard]] SLAB_FORCE_INLINE void* get_raw_ptr() noexcept { 
            return static_cast<void*>(nodes_); 
        }

        SLAB_FORCE_INLINE std::size_t capacity() const noexcept { return Capacity; }

        /** @brief Collective Reclamation. */
        void release_batch(T** ptrs, size_t count) noexcept {
            if (SL_EXPECT_FALSE(count == 0)) return;
            
            // Link the batch together internally first
            for (size_t i = 0; i < count; ++i) {
                if constexpr (!std::is_trivially_destructible_v<T>) ptrs[i]->~T();
                node* n = reinterpret_cast<node*>(ptrs[i]);
                if (i < count - 1) n->next = static_cast<uint32_t>(reinterpret_cast<node*>(ptrs[i+1]) - nodes_);
            }

            node* first = reinterpret_cast<node*>(ptrs[0]);
            node* last = reinterpret_cast<node*>(ptrs[count - 1]);
            uint32_t first_idx = static_cast<uint32_t>(first - nodes_);

            uint64_t current_head = return_head_.load(std::memory_order_acquire);
            for (uint32_t retries = 0; ; ++retries) {
                last->next = epoch_nexus::unpack_index(current_head);
                uint64_t new_head = epoch_nexus::pack(first_idx, epoch_nexus::unpack_epoch(current_head) + 1);
                if (return_head_.compare_exchange_strong(current_head, new_head, std::memory_order_release, std::memory_order_acquire)) break;
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
        }

        void release(T* ptr) noexcept {
            if (!ptr) [[unlikely]] return;
            node* n = reinterpret_cast<node*>(ptr);
            if constexpr (!std::is_trivially_destructible_v<T>) n->data.~T(); 
            
            uint32_t idx = static_cast<uint32_t>(n - nodes_);
            uint64_t current_head = return_head_.load(std::memory_order_acquire);
            for (uint32_t retries = 0; ; ++retries) {
                n->next = epoch_nexus::unpack_index(current_head);
                uint64_t new_head = epoch_nexus::pack(idx, epoch_nexus::unpack_epoch(current_head) + 1);
                if (return_head_.compare_exchange_strong(current_head, new_head, std::memory_order_release, std::memory_order_acquire)) break;
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
        }

        /** @brief Safe ownership collapse for managed handles. */
        SLAB_FORCE_INLINE void release(managed_data<T, mpsc_hybrid_pool>& item) noexcept {
            T* raw = item.release();
            if (raw) [[likely]] release(raw);
        }

        /** @brief Safe ownership collapse for scoped handles. */
        SLAB_FORCE_INLINE void release(scoped_ptr<T>& item) noexcept {
            T* raw = item.release();
            if (raw) [[likely]] release(raw);
        }

        static void deleter_fn(void* ctx, void* raw_ptr) noexcept { 
            if (raw_ptr) [[likely]] static_cast<mpsc_hybrid_pool*>(ctx)->release(static_cast<T*>(raw_ptr)); 
        }

    private:
        /** @brief Producer-side sweep of the return ring. */
        void reclaim_returns() noexcept {
            // Atomically snip the entire return stack in O(1)
            uint64_t current_return = return_head_.exchange(0xFFFFFFFF, std::memory_order_acq_rel);
            uint32_t idx = epoch_nexus::unpack_index(current_return);
            if (idx == 0xFFFFFFFF) return;

            // Traverse the local chain to find the tail
            uint32_t last_idx = idx;
            while (nodes_[last_idx].next != 0xFFFFFFFF) {
                last_idx = nodes_[last_idx].next;
            }

            // Splice the entire return chain back into the allocation head
            uint64_t current_alloc = head_.load(std::memory_order_acquire);
            for (uint32_t retries = 0; ; ++retries) {
                nodes_[last_idx].next = epoch_nexus::unpack_index(current_alloc);
                uint64_t new_alloc = epoch_nexus::pack(idx, epoch_nexus::unpack_epoch(current_alloc) + 1);
                if (head_.compare_exchange_strong(current_alloc, new_alloc, std::memory_order_release, std::memory_order_acquire)) break;
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
            head_.notify_all();
        }
    };
}