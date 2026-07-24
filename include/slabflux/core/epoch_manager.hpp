/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file epoch_manager.hpp
 * @brief Zero-allocation Epoch-Based Reclamation (EBR) orchestrator.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <array>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    template <size_t MaxThreads = 64>
    class alignas(64) epoch_manager {
        struct alignas(64) thread_state {
            std::atomic<uint64_t> active_epoch{0};
        };

        std::array<thread_state, MaxThreads> threads_;
        alignas(64) std::atomic<uint64_t> global_epoch_{1};

    public:
        // Called by a worker thread before accessing lock-free data structures
        SLAB_FORCE_INLINE void enter(size_t thread_id) noexcept {
            uint64_t current = global_epoch_.load(std::memory_order_relaxed);
            threads_[thread_id].active_epoch.store(current, std::memory_order_release);
        }

        // Called by a worker thread when leaving the critical section
        SLAB_FORCE_INLINE void leave(size_t thread_id) noexcept {
            threads_[thread_id].active_epoch.store(0, std::memory_order_release);
        }

        // Called periodically to progress the global epoch horizon
        SLAB_HOT void advance() noexcept {
            global_epoch_.fetch_add(1, std::memory_order_acq_rel);
        }

        // Determines the oldest epoch still being observed by any active thread
        SLAB_HOT uint64_t get_safe_epoch() const noexcept {
            uint64_t oldest_active = global_epoch_.load(std::memory_order_acquire);
            for (size_t i = 0; i < MaxThreads; ++i) {
                uint64_t active = threads_[i].active_epoch.load(std::memory_order_acquire);
                if (active != 0 && active < oldest_active) {
                    oldest_active = active;
                }
            }
            return oldest_active;
        }
        
        SLAB_FORCE_INLINE uint64_t current_epoch() const noexcept {
            return global_epoch_.load(std::memory_order_relaxed);
        }
    };

    // RAII helper for scoping EBR Critical Sections
    template <size_t MaxThreads>
    struct epoch_guard {
        epoch_manager<MaxThreads>& manager;
        size_t tid;
        
        SLAB_FORCE_INLINE epoch_guard(epoch_manager<MaxThreads>& m, size_t t) noexcept : manager(m), tid(t) {
            manager.enter(tid);
        }
        SLAB_FORCE_INLINE ~epoch_guard() noexcept {
            manager.leave(tid);
        }
    };
} // namespace slabflux::core