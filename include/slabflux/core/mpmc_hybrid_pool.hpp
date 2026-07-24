/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/thread_context.hpp"

namespace slabflux::core {

    /**
     * @brief Multi-Producer Multi-Consumer Hybrid Pool.
     * @details A hardware-affine memory manager designed to eliminate cache-line 
     * bouncing on high-concurrency hotspots (e.g., TLS Handshake state management).
     * 
     * Tiered Execution Model:
     * 1. Sharded Stacks: Distributes pointers across N lanes to isolate core contention.
     * 2. Zero-Allocation: Operates on a fixed memory block pre-allocated at ignition.
     * 3. Affinity-Aware: Producers and consumers prefer shard indices mapping to 
     *    their physical core ID.
     */
    template <typename T, std::size_t Capacity, std::size_t Shards = 8>
    class alignas(CACHE_LINE_SIZE) mpmc_hybrid_pool {
        static_assert((Capacity % Shards) == 0, "Capacity must be divisible by Shards to ensure structural symmetry.");
        static constexpr std::size_t SHARD_SIZE = Capacity / Shards;

        struct alignas(CACHE_LINE_SIZE) shard {
            // Indexical top pointer utilizing signed atomic for vacuum signaling
            std::atomic<int32_t> top{static_cast<int32_t>(SHARD_SIZE - 1)};
            T* storage[SHARD_SIZE];
        };

        shard shards_[Shards];
        T memory_matrix_[Capacity];

    public:
        mpmc_hybrid_pool() noexcept {
            // Geometric distribution of pre-allocated memory into the sharded stacks
            for (std::size_t i = 0; i < Capacity; ++i) {
                std::size_t s_idx = i / SHARD_SIZE;
                std::size_t l_idx = i % SHARD_SIZE;
                shards_[s_idx].storage[l_idx] = &memory_matrix_[i];
            }
        }

        /**
         * @brief Acquires a raw object pointer from the pool.
         * @return T* pointer if available, nullptr if the entire matrix is vacuumed.
         */
        SLAB_FORCE_INLINE T* make_raw() noexcept {
            // Deterministic Core-to-Shard mapping via thread_context
            const std::size_t start_shard = core::thread_context::worker_id % Shards;

            for (std::size_t i = 0; i < Shards; ++i) {
                shard& s = shards_[(start_shard + i) % Shards];
                
                int32_t current = s.top.load(std::memory_order_relaxed);
                while (current >= 0) {
                    if (s.top.compare_exchange_weak(current, current - 1,
                                                    std::memory_order_acquire,
                                                    std::memory_order_relaxed)) {
                        return s.storage[current];
                    }
                }
            }
            return nullptr; // Global Exhaustion
        }

        /**
         * @brief Releases an object pointer back into the pool hierarchy.
         */
        SLAB_FORCE_INLINE void release(T* ptr) noexcept {
            if (SL_EXPECT_FALSE(ptr == nullptr)) return;

            // Affinity: Return to the current shard to keep memory hot in local cache
            const std::size_t target_shard = core::thread_context::worker_id % Shards;
            
            for (std::size_t i = 0; i < Shards; ++i) {
                shard& s = shards_[(target_shard + i) % Shards];

                int32_t current = s.top.load(std::memory_order_relaxed);
                while (current < static_cast<int32_t>(SHARD_SIZE - 1)) {
                    if (s.top.compare_exchange_weak(current, current + 1,
                                                    std::memory_order_release,
                                                    std::memory_order_relaxed)) {
                        s.storage[current + 1] = ptr;
                        return;
                    }
                }
            }
        }
    };
} // namespace slabflux::core