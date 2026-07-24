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
 * ============================================================================* @file 03_compile_time_tag_demuxing.cpp
 * @brief Ultra‑low‑latency trading gateway using the slabflux Zero‑Copy Pipeline.
 * * USE CASE: High‑Frequency Trading (HFT). Validating and routing millions of
 * limit orders and market ticks per second with deterministic <10ns overhead.
 */
#include <iostream>
#include <cstdint>
#include <chrono>
#include "slabflux/core/memory.hpp"
#include "slabflux/core/pipeline.hpp"
#include "slabflux/core/demuxer.hpp"
#include "slabflux/meta.hpp"

namespace slabflux::events {
    // 1. Order event (ID = 1, 64‑byte cache‑line isolated)
    struct alignas(64) limit_order {
        static constexpr uint16_t ID = 1;
        uint64_t instrument_id;
        double price;
        uint32_t quantity;
        uint8_t side; // 0 = BUY, 1 = SELL

        limit_order(uint64_t i, double p, uint32_t q, uint8_t s)
            : instrument_id(i), price(p), quantity(q), side(s) {}
    };

    // 2. Market data event (ID = 2)
    struct alignas(64) market_tick {
        static constexpr uint16_t ID = 2;
        uint64_t instrument_id;
        double best_bid;
        double best_ask;

        market_tick(uint64_t i, double bid, double ask)
            : instrument_id(i), best_bid(bid), best_ask(ask) {}
    };
}

// --- HFT STRATEGY LAYERS (Duck‑typed Handlers) ---

// 1. Risk manager: “Fat‑finger” protection. If the trader sends nonsense, drop it immediately.
struct pre_trade_risk {
    inline bool on(slabflux::events::limit_order& order) noexcept {
        if (order.quantity > 100000 || order.price <= 0.01) {
            std::cout << "[RISK REJECT] Fat-finger detected! Order dropped. Qty: "
            << order.quantity << "\n";
            return false; // SHORT-CIRCUIT
        }
        return true;
    }

    // Market data is not filtered; it just passes through
    inline bool on(slabflux::events::market_tick*) noexcept { return true; }
};

// 2. Execution engine: Sends the validated order to NASDAQ / Binance
struct execution_router {
    inline void on(slabflux::events::limit_order& order) noexcept {
        std::cout << "[EXECUTION] Routing Order to Exchange -> Instrument: "
        << order.instrument_id << " | Price: $" << order.price << "\n";
    }

    inline void on(slabflux::events::market_tick& tick) noexcept {
        std::cout << "[MARKET DATA] Updating internal book -> Instrument: "
        << tick.instrument_id << " | Spread: "
        << (tick.best_ask - tick.best_bid) << "\n";
    }
};

// --- RUNTIME ENVIRONMENT ---
int main() {
    using namespace slabflux::core;
    using namespace slabflux::events;

    // Build the Gateway
    pre_trade_risk risk_layer;
    execution_router algo_router;
    pipeline hft_gateway(risk_layer, algo_router);

    // Explicitly define the transport-layer bus signature
    using ingress_bus = demuxer<limit_order, market_tick>;

    // Imagine these come from the memory pool via an FPGA network card
    limit_order safe_order(9921, 150.25, 100, 0);       // Valid buy order
    limit_order toxic_order(9921, 150.00, 5000000, 1);  // 5M‑share sell (Fat finger)
    market_tick msft_tick(9921, 150.20, 150.25);        // Price update

    std::cout << "=== Market Open (HFT Gateway) ===\n\n";

    // 1. Incoming market data
    tagged_pointer p1 = tagged_pointer::pack(market_tick::ID, &msft_tick);
    ingress_bus::route(p1, hft_gateway);

    // 2. Incoming normal order from the algo
    tagged_pointer p2 = tagged_pointer::pack(limit_order::ID, &safe_order);
    ingress_bus::route(p2, hft_gateway);

    // 3. Incoming faulty/dangerous order
    std::cout << "\n[!] Trader is trying to dump 5 million shares...\n";
    tagged_pointer p3 = tagged_pointer::pack(limit_order::ID, &toxic_order);
    ingress_bus::route(p3, hft_gateway); // Risk layer cuts it off immediately!

    return 0;
}
