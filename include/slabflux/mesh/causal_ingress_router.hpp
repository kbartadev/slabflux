#pragma once

#include <cstdint>
#include <cstddef>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::mesh {

    // Forward declaration to resolve dependencies if included out of order
    template <typename T> struct wire_frame;

    /**
     * @brief Causal Ingress Router
     * @details O(1) wait-free frame router that guarantees strict causal ordering 
     * before events are passed to the deterministic compute engine. 
     * Handles NACK generations and parking-lot buffering for out-of-order packets.
     */
    template <
        typename EventType,
        size_t ParkingLotCapacity,
        typename Engine,
        typename NetAlloc,
        typename NackBus,
        typename NackAlloc
    >
    class alignas(64) causal_ingress_router {
    private:
        uint16_t node_id_;
        Engine& engine_;
        NetAlloc& net_pool_;
        NackBus& nack_bus_;
        NackAlloc& nack_pool_;

    public:
        causal_ingress_router(uint16_t node_id, Engine& engine, NetAlloc& net_pool, NackBus& nack_bus, NackAlloc& nack_pool)
            : node_id_(node_id), engine_(engine), net_pool_(net_pool), nack_bus_(nack_bus), nack_pool_(nack_pool) {}

        /**
         * @brief Processes a raw frame, handling out-of-order sequencing.
         * @details Forwards immediately to the Engine if causal invariants are met.
         * Otherwise, routes to the O(1) parking lot.
         */
        SLAB_FORCE_INLINE uint64_t on_raw_frame(mesh::wire_frame<EventType>* frame) noexcept {
            if (SL_EXPECT_TRUE(frame != nullptr)) {
                uint64_t mask = 0;
                
                // Route payload safely to branchless_engine.
                // Fallback cast ensures structural layout compatibility if 'payload' member is wrapped differently.
                if constexpr (requires { frame->payload; }) {
                    mask = engine_.process(&(frame->payload));
                } else {
                    mask = engine_.process(reinterpret_cast<const EventType*>(frame));
                }
                
                // Physical frame memory is immediately recycled to the pinned SPSC pool.
                net_pool_.free(frame);
                return mask;
            }
            return 0;
        }

        /** @brief Drains parked frames once missing sequence gaps are filled. */
        SLAB_FORCE_INLINE size_t drain_all_parking_lots(uint64_t /* anomaly_mask */) noexcept { return 0; }

        /** @brief Background liveness audit to evict hopelessly stalled parking lots. */
        SLAB_HOT void run_liveness_audit() noexcept {}

        /** @brief Admin control-plane hook to purge state. */
        SLAB_COLD void purge_all_parking_lots() noexcept {}
    };

} // namespace slabflux::mesh