/**
 * @file pipeline_hierarchical_dispatch_chain.cpp
 * @brief Validation of vertical event propagation across type hierarchies.
 */

#include <gtest/gtest.h>
#include "slabflux/core.hpp"

namespace slabflux::test {

    // --- Domain Definitions ---
    struct network_frame {
        uint32_t sequence_id;
    };

    struct transport_packet : public network_frame {
        uint16_t port;
    };

    struct security_alert : public transport_packet {
        uint8_t threat_level;
    };

    // --- Handler Implementation ---
    struct forensics_engine {
        int frames_traced = 0;
        int packets_inspected = 0;
        int alerts_processed = 0;

        // Layer 1: Network abstraction
        void on(const network_frame& ev) {
            frames_traced++;
        }

        // Layer 2: Transport abstraction
        void on(const transport_packet& ev) {
            packets_inspected++;
        }

        // Layer 3: Specific Security Event
        void on(const security_alert& ev) {
            alerts_processed++;
        }
    };

    TEST(VerticalDispatchTest, validates_top_down_unwinding) {
        pool<security_alert, 16> alert_pool;
        forensics_engine engine;
        pipeline<forensics_engine> pipe(engine);

        auto ev = alert_pool.make();
        ev->threat_level = 5;

        // dispatch by reference
        pipe.dispatch(*ev);

        EXPECT_EQ(engine.alerts_processed, 1);
        EXPECT_EQ(engine.packets_inspected, 1);
        EXPECT_EQ(engine.frames_traced, 1);
    }
}
