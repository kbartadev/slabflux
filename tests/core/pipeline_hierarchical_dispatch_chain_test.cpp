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
 * ============================================================================* @file pipeline_hierarchical_dispatch_chain.cpp
 * @brief Validation of vertical event propagation across type hierarchies.
 */

#include <gtest/gtest.h>
#include "slabflux/core.hpp"

namespace slabflux::test {

    // --- Domain Definitions ---
    struct network_frame {
        uint32_t sequence_id;
    };
    EXPECT_INHERITANCE(network_frame);

    struct transport_packet : public network_frame {
        uint16_t port;
    };
    EXPECT_INHERITANCE(transport_packet, network_frame);

    struct security_alert : public transport_packet {
        uint8_t threat_level;
    };
    EXPECT_INHERITANCE(security_alert, transport_packet);

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
    EXPECT_INHERITANCE(forensics_engine);

    TEST(VerticalDispatchTest, validates_top_down_unwinding) {
        pool<security_alert, 16> alert_pool;
        forensics_engine engine;
        pipeline pipe(engine);

        auto ev = alert_pool.make();
        ev->threat_level = 5;

        // dispatch by reference
        pipe.dispatch(*ev);

        EXPECT_EQ(engine.alerts_processed, 1);
        EXPECT_EQ(engine.packets_inspected, 1);
        EXPECT_EQ(engine.frames_traced, 1);
    }
}
