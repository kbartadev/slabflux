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
 * ============================================================================* @file test_omnibus_integration.cpp
 * @brief Complex validation of joint vertical and horizontal dispatching.
 * Reality is multi-dimensional; our pipeline must reflect both type and stage.
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "slabflux/core.hpp"

namespace slabflux::test {

    // --- 1. VERTICAL HIERARCHY ---

    struct market_message {
        uint64_t receive_time_ns;
    };
    EXPECT_INHERITANCE(market_message);

    struct trade_signal : public market_message {
        uint32_t ticker_id;
    };
    EXPECT_INHERITANCE(trade_signal, market_message);

    struct limit_order : public trade_signal {
        double price;
        uint32_t qty;
        limit_order(uint32_t id, double p, uint32_t q) : price(p), qty(q) {
            ticker_id = id;
        }
    };
    EXPECT_INHERITANCE(limit_order, trade_signal);

    // --- 2. HORIZONTAL HANDLERS ---

    // Handler A: Low-level Infrastructure (Only cares about base timing)
    struct latency_monitor {
        int timing_points = 0;
        void on(const market_message& ev) {
            timing_points++;
        }
    };
    EXPECT_INHERITANCE(latency_monitor);

    // Handler B: Domain Logic (Checks if we trade this ticker)
    struct ticker_guard {
        bool ticker_allowed = false;
        void on(const trade_signal& ev) {
            if (ev.ticker_id == 1001) ticker_allowed = true;
        }
    };
    EXPECT_INHERITANCE(ticker_guard);

    // Handler C: Specialist (Executes the actual limit order)
    struct order_book {
        double last_executed_price = 0.0;
        void on(const limit_order& ev) {
            last_executed_price = ev.price;
        }
    };
    EXPECT_INHERITANCE(order_book);

    // --- 3. OMNIBUS TEST CASE ---

    TEST(OmnibusIntegrationTest, validates_matrix_dispatch_flow) {
        // Setup memory and components
        pool<limit_order, 16> order_pool;
        latency_monitor infra;
        ticker_guard domain;
        order_book execution;

        // Construct the Horizontal Pipeline with CTAD deduction
        pipeline pipeline_matrix(infra, domain, execution);

        // Create the event
        auto order = order_pool.make(1001, 552.10, 100);
        order->receive_time_ns = 123456789;

        // ACTION: Dispatch the 3D event through the 3-stage pipeline
        pipeline_matrix.dispatch(order);

        // VERIFICATION:

        // 1. Vertical check: Did the base-class handler (latency_monitor) see the event?
        EXPECT_EQ(infra.timing_points, 1);

        // 2. Mid-level check: Did the domain handler (ticker_guard) see the trade_signal?
        EXPECT_TRUE(domain.ticker_allowed);

        // 3. Leaf-level check: Did the specialist (order_book) process the final price?
        EXPECT_DOUBLE_EQ(execution.last_executed_price, 552.10);
    }
}
