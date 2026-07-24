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
 * ============================================================================* @file pipeline_demuxer.cpp
 * @brief Unit tests for the compile-time branchless core::demuxer bus.
 */

#include <gtest/gtest.h>
#include <iostream>
#include <cstdint>
#include <vector>
#include <string>

#include "slabflux/core/memory.hpp"
#include "slabflux/core/pipeline.hpp"
#include "slabflux/core/demuxer.hpp"
#include "slabflux/meta.hpp"

using namespace slabflux::core;

// --- 1. TEST EVENTS WITH STATIC PACKET IDs ---
namespace slabflux::events {
    struct d_order {
        static constexpr uint16_t ID = 501;
        uint64_t id;
        double price;
        d_order(uint64_t i, double p) : id(i), price(p) {}
    };
    EXPECT_INHERITANCE(d_order);

    struct d_cancel {
        static constexpr uint16_t ID = 502;
        uint64_t id;
        d_cancel(uint64_t i) : id(i) {}
    };
    EXPECT_INHERITANCE(d_cancel);

    struct d_unregistered {
        static constexpr uint16_t ID = 999;
        int dummy;
        d_unregistered(int dummy) : dummy(dummy) {}
    };
    EXPECT_INHERITANCE(d_unregistered);
}

// --- 2. EXECUTION STRATEGIES (DUCK-TYPED HANDLERS) ---
struct DemuxTestHandler {
    std::vector<std::string> execution_log;
    uint64_t last_processed_id = 0;

    bool on(slabflux::events::d_order* order) noexcept {
        execution_log.push_back("ORDER_PROCESSED");
        last_processed_id = order->id;
        return true;
    }

    bool on(slabflux::events::d_cancel* cancel) noexcept {
        execution_log.push_back("CANCEL_PROCESSED");
        last_processed_id = cancel->id;
        return true;
    }
};
EXPECT_INHERITANCE(DemuxTestHandler);

// --- 3. GOOGLETEST SUITE FOR COMPILE-TIME DISPATCH ---

TEST(PipelineDemuxerTest, BranchlessCompilationAndRoutingFlawless) {
    using namespace slabflux::events;

    DemuxTestHandler handler;
    pipeline pipe(handler);

    // Explicitly define the ingress conduit signature with supported types
    using ingress_bus = demuxer<d_order, d_cancel>;

    // ------------------------------------------------------------------------
    // CASE 1: Route Order Packet via tagged_pointer
    // ------------------------------------------------------------------------
    d_order order_evt(8899, 1050.75);
    tagged_pointer tp1 = tagged_pointer::pack(d_order::ID, &order_evt);

    ingress_bus::route(tp1, pipe);

    ASSERT_EQ(handler.execution_log.size(), 1);
    EXPECT_EQ(handler.execution_log[0], "ORDER_PROCESSED");
    EXPECT_EQ(handler.last_processed_id, 8899);

    // ------------------------------------------------------------------------
    // CASE 2: Route Cancel Packet via tagged_pointer
    // ------------------------------------------------------------------------
    d_cancel cancel_evt(9900);
    tagged_pointer tp2 = tagged_pointer::pack(d_cancel::ID, &cancel_evt);

    ingress_bus::route(tp2, pipe);

    ASSERT_EQ(handler.execution_log.size(), 2);
    EXPECT_EQ(handler.execution_log[1], "CANCEL_PROCESSED");
    EXPECT_EQ(handler.last_processed_id, 9900);
}

TEST(PipelineDemuxerTest, UnregisteredTagIsSafelyIgnoredByFoldExpression) {
    using namespace slabflux::events;

    DemuxTestHandler handler;
    pipeline pipe(handler);

    using ingress_bus = demuxer<d_order, d_cancel>;

    // An event type that is NOT part of the demuxer's template pack
    d_unregistered unreg_evt{42};
    tagged_pointer tp_invalid = tagged_pointer::pack(d_unregistered::ID, &unreg_evt);

    // Should not compile-error, should not invoke any handler, should log to stderr in NDEBUG
    ingress_bus::route(tp_invalid, pipe);

    // Verify that execution matrix remained completely untouched (short-circuited at demuxer level)
    EXPECT_EQ(handler.execution_log.size(), 0);
    EXPECT_EQ(handler.last_processed_id, 0);
}
