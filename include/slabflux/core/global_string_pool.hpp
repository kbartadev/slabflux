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
#include <system_error>
#include <immintrin.h> // For _mm_prefetch, _mm_pause
#include "string_chunk.hpp"
#include "string_service.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/platform/os.hpp"

namespace slabflux::core {

    /** @brief Concurrency tags for specialized string handling. */
    enum class string_concurrency { spsc, mpmc };

    // ============================================================
    // GLOBAL LOCK-FREE STRING POOL
    // ============================================================
    /**
     * @brief String Matrix Pool
     * @details Optimized to provide O(1) index-to-pointer math.
     * Enforces physical residency via a contiguous storage block.
     * using HugePages and mlock, with graceful fallback to standard locked pages if HugePage allocation fails.
     */    
    template <std::size_t Capacity = 65536, string_concurrency Mode = string_concurrency::mpmc>
    class string_matrix_pool {
        string_chunk* storage_{ nullptr };

        // MPMC Mode: Optimized versioned counter for ABA protection
        alignas(64) std::atomic<uint64_t> free_head_{ 0xFFFFFFFF };

        // SPSC Mode: Zero-atomic head for pin-to-pin optimal paths
        uint32_t local_head_{ string_chunk::END_OF_CHAIN };

        // Structural Residency State:
        // 0 = Uninitialized, 1 = Initializing (In-Flight), 2 = Initialized (Residency Achieved)
        std::atomic<uint8_t> init_state_{ 0 };

        size_t total_bytes_{ 0 };

    public:
        using value_type = string_chunk;

        string_matrix_pool() = default;
        ~string_matrix_pool() {
            if (storage_) {
#ifdef _WIN32
                VirtualFree(storage_, 0, MEM_RELEASE);
#else
                munlock(storage_, total_bytes_);
                munmap(storage_, total_bytes_);
#endif
            }
        }

        void initialize(uint32_t max_chunks = Capacity) {
            uint8_t expected = 0;
            if (init_state_.compare_exchange_strong(expected, 1)) {
                total_bytes_ = static_cast<size_t>(max_chunks) * sizeof(string_chunk);

#ifdef _WIN32
                void* mem = VirtualAlloc(nullptr, total_bytes_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
                // Allocation: Enforce physical residency and alignment
                int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED | MAP_HUGETLB | MAP_HUGE_2MB;
                void* mem = ::mmap(nullptr, total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);

                if (mem == MAP_FAILED) {
                    flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE | MAP_LOCKED;
                    mem = ::mmap(nullptr, total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
                }

                if (mem == MAP_FAILED) {
                    // Permissive Fallback: If physical pinning fails (ulimit -l), use standard virtual memory.
                    flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE;
                    mem = ::mmap(nullptr, total_bytes_, PROT_READ | PROT_WRITE, flags, -1, 0);
                }

                if (mem != MAP_FAILED && mem != nullptr) {
                    ::madvise(mem, total_bytes_, MADV_HUGEPAGE | MADV_DONTDUMP);
                    if (flags & MAP_LOCKED) {
                        ::mlock(mem, total_bytes_);
                    }
                }
#endif
                if (!mem || mem == MAP_FAILED) {
                    init_state_.store(0, std::memory_order_relaxed);
                    throw std::bad_alloc();
                }

                storage_ = static_cast<string_chunk*>(mem);

                for (uint32_t i = 0; i < max_chunks - 1; ++i) {
                    storage_[i].next_chunk_idx = i + 1;
                }
                storage_[max_chunks - 1].next_chunk_idx = string_chunk::END_OF_CHAIN;

                if constexpr (Mode == string_concurrency::mpmc) {
                    // Activate the head with release semantics
                    free_head_.store(0, std::memory_order_release);
                } else {
                    local_head_ = 0;
                }
                init_state_.store(2, std::memory_order_release);
            } else {
                // Multi-Threaded Sync: If another thread is actively initializing the singleton,
                // spin-wait until the residency barrier is cleared to prevent nullptr dereferences.
                while (init_state_.load(std::memory_order_acquire) == 1) {
                    _mm_pause();
                }
            }
        }

        // Lazy init (in case someone forgets to call it in main)
        SLAB_HOT void ensure_init() {
            if (SL_EXPECT_FALSE(init_state_.load(std::memory_order_acquire) != 2)) {
                initialize();
            }
        }

        string_chunk* make_raw() {
            ensure_init();

            if constexpr (Mode == string_concurrency::spsc) {
                // Shatter-level SPSC Optimality: Zero CAS, Zero atomic loops.
                if (local_head_ == string_chunk::END_OF_CHAIN) [[unlikely]] return nullptr;
                uint32_t idx = local_head_;
                local_head_ = storage_[idx].next_chunk_idx;
                return &storage_[idx];
            } else {
                uint64_t head = free_head_.load(std::memory_order_acquire);
                for (uint32_t retries = 0; ; ++retries) {
                    uint32_t idx = head & 0xFFFFFFFF;
                    if (idx == string_chunk::END_OF_CHAIN) [[unlikely]] return nullptr;

                    uint32_t next = storage_[idx].next_chunk_idx;
                    uint64_t new_head = ((head >> 32) + 1) << 32 | next;

                    // Memory Fabric Optimization: Prefetch the next free chunk to eliminate MESI stalls in the subsequent cycle.
                    if (next != string_chunk::END_OF_CHAIN) [[likely]]
                        _mm_prefetch(reinterpret_cast<const char*>(&storage_[next]), _MM_HINT_T0);

                    if (free_head_.compare_exchange_strong(
                        head, new_head,
                        std::memory_order_release,
                        std::memory_order_acquire)) return &storage_[idx];
                    
                    for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
                }
            }
        }

        /**
         * @brief Amortized Batch Allocation.
         * @details Reduces atomic tax from O(N) to O(1) by grabbing a chain 
         * of chunks in a single transaction.
         * @return Number of chunks successfully acquired.
         */
        size_t make_batch(string_chunk** out_ptrs, size_t count) noexcept {
            ensure_init();
            if (SL_EXPECT_FALSE(count == 0)) return 0;

            if constexpr (Mode == string_concurrency::spsc) {
                size_t allocated = 0;
                while (allocated < count && local_head_ != string_chunk::END_OF_CHAIN) {
                    uint32_t idx = local_head_;
                    out_ptrs[allocated] = &storage_[idx];
                    local_head_ = storage_[idx].next_chunk_idx;
                    allocated++;
                }
                return allocated;
            } else {
                uint64_t head = free_head_.load(std::memory_order_acquire);
                for (uint32_t retries = 0; ; ++retries) {
                    uint32_t first_idx = head & 0xFFFFFFFF;
                    if (first_idx == string_chunk::END_OF_CHAIN) [[unlikely]] return 0;

                    uint32_t last_idx = first_idx;
                    size_t actual_count = 1;
                    while (actual_count < count) {
                        uint32_t next = storage_[last_idx].next_chunk_idx;
                        if (next == string_chunk::END_OF_CHAIN) break;
                        last_idx = next;
                        actual_count++;
                    }

                    uint32_t remaining_head = storage_[last_idx].next_chunk_idx;
                    uint64_t new_head = ((head >> 32) + 1) << 32 | remaining_head;

                    if (SL_EXPECT_TRUE(free_head_.compare_exchange_strong(head, new_head,
                        std::memory_order_release, std::memory_order_acquire))) {
                        
                        // Address Materialization: Resolve physical indices into pointers while maintaining temporal residency in L1-D.
                        uint32_t curr = first_idx;
                        for (size_t i = 0; i < actual_count; ++i) {
                            out_ptrs[i] = &storage_[curr];
                            curr = storage_[curr].next_chunk_idx;
                        }
                        return actual_count;
                    }
                    
                    // Interconnect Stabilization: Capped backoff prevents interconnect saturation during bulk reclamation.
                    for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
                }
            }
        }

        /** @brief Collective Reclamation: Returns a linked batch of chunks to the pool. */
        void release_batch(string_chunk** ptrs, size_t count) noexcept {
            if (SL_EXPECT_FALSE(count == 0)) return;
            for (size_t i = 0; i < count; ++i) release(ptrs[i]);
        }

        void release(string_chunk* p) noexcept {
            if (!p) return;
            uint32_t idx = get_index(p);
            if constexpr (Mode == string_concurrency::spsc) {
                p->next_chunk_idx = local_head_;
                local_head_ = idx;
            } else {
                uint64_t head = free_head_.load(std::memory_order_acquire);
                for (uint32_t retries = 0; ; ++retries) {
                    p->next_chunk_idx = head & 0xFFFFFFFF;
                    uint64_t new_head = ((head >> 32) + 1) << 32 | idx;
                    if (free_head_.compare_exchange_strong(
                        head, new_head,
                        std::memory_order_release,
                        std::memory_order_acquire)) break;
                    for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
                }
            }
        }

        // --- Contract Aliases ---
        string_chunk* make() { return make_raw(); }
        void free(string_chunk* p) noexcept { release(p); }

        // --- Optimized Index Math ---
        SLAB_FORCE_INLINE uint32_t get_index(const string_chunk* p) const noexcept {
            return static_cast<uint32_t>(p - storage_);
        }

        SLAB_FORCE_INLINE string_chunk* get_by_index(uint32_t i) noexcept {
            return (i == string_chunk::END_OF_CHAIN) ? nullptr : &storage_[i];
        }

        SLAB_FORCE_INLINE const string_chunk* get_by_index_const(uint32_t i) const noexcept {
            return (i == string_chunk::END_OF_CHAIN) ? nullptr : &storage_[i];
        }
    };

    // --- Authoritative specialized pools ---
    template <std::size_t Cap> using mpmc_matrix_pool = string_matrix_pool<Cap, string_concurrency::mpmc>;
    template <std::size_t Cap> using spsc_matrix_pool = string_matrix_pool<Cap, string_concurrency::spsc>;

    /** @brief Default Global Pool (MPMC for cross-core safety). */
    using default_string_pool = mpmc_matrix_pool<65536>;

    // --- SINGLETON ACCESS ---
    inline default_string_pool& get_global_string_pool() {
        static default_string_pool instance;
        return instance;
    }

    inline string_service<default_string_pool>& get_global_string_service() {
        static string_service<default_string_pool> instance(get_global_string_pool());
        return instance;
    }

    // --- Smart String Service Aliases ---
    template <std::size_t Cap> using spsc_smart_string = string_service<spsc_matrix_pool<Cap>>;
    template <std::size_t Cap> using mpmc_smart_string = string_service<mpmc_matrix_pool<Cap>>;
    template <std::size_t Cap> using smart_string = mpmc_smart_string<Cap>;

    // Developer interface for main()
    inline void init_global_string_pool(uint32_t capacity) {
        get_global_string_pool().initialize(capacity);
    }
} // namespace slabflux::core
