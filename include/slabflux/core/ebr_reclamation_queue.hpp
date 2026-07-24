/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file ebr_reclamation_queue.hpp
 * @brief Deferred Lock-Free Graveyard for Epoch-Based Reclamation.
 */

#pragma once
#include <cstdint>
#include "slabflux/core/spsc_ring_conduit.hpp"

namespace slabflux::core {

    template <typename T, size_t Capacity = 4096>
    class alignas(64) ebr_reclamation_queue {
        struct deferred_node {
            T* ptr;
            uint64_t retirement_epoch;
        };

        spsc_ring_conduit<deferred_node, Capacity> graveyard_;

    public:
        // Submits the pointer for deferred recycling securely bound to the provided epoch
        SLAB_HOT bool retire(T* ptr, uint64_t current_epoch) noexcept {
            auto* slot = graveyard_.get_reserved_slot(0);
            if (SL_EXPECT_FALSE(!slot)) return false; 
            
            slot->ptr = ptr;
            slot->retirement_epoch = current_epoch;
            graveyard_.commit_n(1);
            return true;
        }

        // Evaluates the chronological queue and recycles proven-safe pointers back to the target pool
        template <typename PoolType>
        SLAB_HOT void scavenge(uint64_t safe_epoch, PoolType& pool) noexcept {
            size_t available = graveyard_.available_to_peek();
            size_t reclaimed = 0;

            for (size_t i = 0; i < available; ++i) {
                const auto* node = graveyard_.get_peek_slot(i);
                if (node->retirement_epoch < safe_epoch) {
                    pool.release(node->ptr);
                    reclaimed++;
                } else break; // Order is strictly chronological
            }

            if (reclaimed > 0) graveyard_.consume_n(reclaimed);
        }
    };
} // namespace slabflux::core