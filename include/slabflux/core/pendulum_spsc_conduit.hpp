/*
 * @file pendulum_spsc_conduit.hpp
 * @brief Oscillating Wavefront (Boustrophedon) SPSC Queue.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    /**
     * @brief Pendulum-Wavefront based, Zero-CAS SPSC Data Channel.
     * @details Eliminates ring-buffer prefetcher disruption and cache-line 
     * bouncing caused by shared metadata.
     */
    template <typename T, std::size_t Capacity>
    class alignas(CACHE_LINE_SIZE) pendulum_spsc_conduit {
        // At least 3 slots are required for meaningful oscillation without 
        // threads being constantly trapped on the same cache line.
        static_assert(Capacity >= 3, "Pendulum architecture requires Capacity >= 3");

    private:
        // The spacetime grid: the only shared memory area.
        // Threads communicate physically here, but move in spatial isolation.
        alignas(CACHE_LINE_SIZE) std::atomic<T*> slots_[Capacity];

        // Strict local cursor state. Neither the producer nor the consumer 
        // sees the other's state.
        struct alignas(CACHE_LINE_SIZE) cursor_state {
            std::ptrdiff_t index{0};
            std::ptrdiff_t stride{1};
        };

        // Isolated to separate cache lines to avoid False Sharing
        alignas(CACHE_LINE_SIZE) cursor_state p_state_;
        alignas(CACHE_LINE_SIZE) cursor_state c_state_;

        /**
         * @brief Boustrophedon traversal: Immediate direction change upon boundary contact.
         */
        static constexpr void advance(cursor_state& state) noexcept {
            state.index += state.stride;
            if (SL_UNLIKELY(state.index == static_cast<std::ptrdiff_t>(Capacity))) {
                state.stride = -1;
                state.index = static_cast<std::ptrdiff_t>(Capacity) - 2;
            } else if (SL_UNLIKELY(state.index == -1)) {
                state.stride = 1;
                state.index = 1;
            }
        }

    public:
        pendulum_spsc_conduit() {
            for (std::size_t i = 0; i < Capacity; ++i) {
                slots_[i].store(nullptr, std::memory_order_relaxed);
            }
        }

        ~pendulum_spsc_conduit() = default;
        pendulum_spsc_conduit(const pendulum_spsc_conduit&) = delete;
        pendulum_spsc_conduit& operator=(const pendulum_spsc_conduit&) = delete;

        /**
         * @brief Wait-Free, Zero-CAS Push.
         * @return True if successful. False if the queue is full.
         */
        [[nodiscard]] bool try_push(T* payload) noexcept {
            // Predictive read: the slot must be nullptr
            T* expected_empty = slots_[p_state_.index].load(std::memory_order_acquire);
            if (SL_EXPECT_FALSE(expected_empty != nullptr)) {
                return false; // Saturation (Producer caught up with Consumer's trail)
            }

            // Write data and publish to memory bus
            slots_[p_state_.index].store(payload, std::memory_order_release);
            
            // Update local position
            advance(p_state_);
            
            return true;
        }

        /**
         * @brief Wait-Free, Lock-Free Pop.
         * @return True if data extracted. False if the queue is empty.
         */
        [[nodiscard]] bool try_pop(T*& out_payload) noexcept {
            // Predictive read: waiting for data from the adjacent plane
            T* payload = slots_[c_state_.index].load(std::memory_order_acquire);
            if (SL_EXPECT_FALSE(payload == nullptr)) {
                return false; // Starvation (Consumer caught up with Producer's trail)
            }

            // Data extraction
            out_payload = payload;

            // Release slot (Reset to Vacuum state)
            slots_[c_state_.index].store(nullptr, std::memory_order_release);
            
            // Update local position
            advance(c_state_);
            
            return true;
        }
    };

} // namespace slabflux::core
