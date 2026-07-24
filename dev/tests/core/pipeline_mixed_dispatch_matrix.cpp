/**
 * @file test_omnibus_integration.cpp
 * @brief Complex validation of joint vertical and horizontal dispatching.
 * Reality is multi-dimensional; our pipeline must reflect both type and stage.
 */

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include "slabflux/core.hpp"

namespace slabflux::test {

    // --- 1. VERTICAL HIERARCHY (The "What") ---

    struct market_message {
        uint64_t receive_time_ns;
    };

    struct trade_signal : public market_message {
        uint32_t ticker_id;
    };

    struct limit_order : public trade_signal {
        double price;
        uint32_t qty;
        limit_order(uint32_t id, double p, uint32_t q) : price(p), qty(q) {
            ticker_id = id;
        }
    };

    // --- 2. HORIZONTAL HANDLERS (The "Who") ---

    // Handler A: Low-level Infrastructure (Only cares about base timing)
    struct latency_monitor {
        int timing_points = 0;
        void on(const market_message& ev) {
            timing_points++;
        }
    };

    // Handler B: Domain Logic (Checks if we trade this ticker)
    struct ticker_guard {
        bool ticker_allowed = false;
        void on(const trade_signal& ev) {
            if (ev.ticker_id == 1001) ticker_allowed = true;
        }
    };

    // Handler C: Specialist (Executes the actual limit order)
    struct order_book {
        double last_executed_price = 0.0;
        void on(const limit_order& ev) {
            last_executed_price = ev.price;
        }
    };

    // --- 3. OMNIBUS TEST CASE ---

    TEST(MixedMatrixTest, validates_cross_dimensional_dispatch) {
        // Setup memory and components
        pool<limit_order, 16> order_pool;
        latency_monitor infra;
        ticker_guard domain;
        order_book execution;

        // Construct the Horizontal Pipeline
        // The order: Infrastructure -> Domain -> Execution
        pipeline<latency_monitor, ticker_guard, order_book> pipeline_matrix(infra, domain, execution);

        // Create the event
        auto order = order_pool.make(1001, 552.10, 100);
        order->receive_time_ns = 123456789;

        // ACTION: Dispatch the 3D event through the 3-stage pipeline
        pipeline_matrix.dispatch(*order);

        // VERIFICATION:

        // 1. Vertical check: Did the base-class handler (latency_monitor) see the event?
        EXPECT_EQ(infra.timing_points, 1);

        // 2. Mid-level check: Did the domain handler (ticker_guard) see the trade_signal?
        EXPECT_TRUE(domain.ticker_allowed);

        // 3. Leaf-level check: Did the specialist (order_book) process the final price?
        EXPECT_DOUBLE_EQ(execution.last_executed_price, 552.10);
    }
}