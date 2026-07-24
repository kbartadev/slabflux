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
 * ============================================================================* @file 05_bimodal_hft_integration.cpp
 * @brief Industrial Bimodal Architecture: 100M/sec Bulk Ingestion + Complex Event Escalation
 */

#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <immintrin.h>
#include "slabflux/core.hpp"
#include "slabflux/bridge/bridge_sync.hpp"

using namespace slabflux;
using namespace slabflux::core;
using namespace slabflux::bridge;

// ============================================================================
// L0: WIRE LAYER
// ============================================================================
struct market_tick {
    uint32_t instrument_id;
    float    price;
    float    volume;
};

// ============================================================================
// L2: THE “SCALPEL” (Complex Event Pipeline)
// Works with pure C++ references, metaprogramming (shadow mapping) is happy!
// ============================================================================
struct volatility_filter {
    bool on(const market_tick& ev) {
        if (ev.volume < 5000.0f) return false; // Short-circuit
        std::cout << "[L2 RISK] Anomaly on ID " << ev.instrument_id
                  << " | Vol: " << ev.volume << " | Price: $" << ev.price << "\n";
        return true;
    }
};

struct trading_halt_execution {
    void on(const market_tick& ev) {
        std::cout << "[L3 EXEC] TRIGGERING CIRCUIT BREAKER FOR ID "
                  << ev.instrument_id << "!\n";
    }
};

// Alias for easier reference
using bimodal_pipeline = pipeline<volatility_filter, trading_halt_execution>;

// ============================================================================
// L1: THE “GRINDER” (100M/sec Fast-Path Ingestion)
// Perfectly matches the expectations of bridge_sync.hpp!
// ============================================================================
struct order_book_updater {
    bimodal_pipeline& slow_pipe;

    // Inject the pipeline
    explicit order_book_updater(bimodal_pipeline& pipe) : slow_pipe(pipe) {}

    // bridge_sync::publish() EXPECTS EXACTLY this signature
    SLAB_FORCE_INLINE void process(const market_tick& tick,
                                   uint64_t lsn,
                                   float* positions) noexcept {
        float old_price = positions[tick.instrument_id];
        positions[tick.instrument_id] = tick.price; // AVX-friendly array update

        // Escalation condition (branchless-friendly)
        if (old_price > 0.0f && tick.price < old_price * 0.95f) [[unlikely]] {
            // Escalate to L2: the Pipeline receives a pure reference!
            slow_pipe.dispatch(tick);
        }
    }
};

// ============================================================================
// SYNC CONTEXT (for bridge_sync.hpp)
// ============================================================================
struct sync_context {
    std::atomic<uint64_t> counter{ 1 };
    uint64_t reserve_next() { return counter.fetch_add(1, std::memory_order_relaxed); }
};

// ============================================================================
// ORCHESTRATION
// ============================================================================
int main() {
    std::cout << "=== Bimodal HFT Engine Integration ===\n\n";

    // 1. Engine Pool & Bridge (spsc_event_bridge handles pool cleanup!)
    spsc_pool<market_tick, 4096> engine_pool;
    spsc_event_bridge<market_tick, 4096> bridge(engine_pool);

    // 2. Handlers
    volatility_filter      risk_gate;
    trading_halt_execution circuit_breaker;
    bimodal_pipeline       complex_matrix(risk_gate, circuit_breaker);

    // 3. The Grinder and the Context
    order_book_updater fast_updater(complex_matrix);
    sync_context       ctx;

    std::atomic<bool> running{true};

    // --- CONSUMER THREAD ---
    std::thread engine_thread([&]() {
        while (running.load(std::memory_order_relaxed)) {
            // The Bridge automatically drives the fast_updater with raw pointers,
            // then uses static_deleter to return memory to the pool.
            bridge.consume(fast_updater, ctx);
            _mm_pause();
        }
        bridge.consume(fast_updater, ctx);
    });

    // --- PRODUCER HELPER ---
    auto push_tick = [&](uint32_t id, float price, float vol) {
        auto ev = engine_pool.make();
        ev->instrument_id = id;
        ev->price = price;
        ev->volume = vol;
        bridge.send(ev);
    };

    std::cout << "--- SCENARIO 1: Bulk Normal Ticks (100M/sec path) ---\n";
    push_tick(1, 100.0f, 1000.0f);
    push_tick(1, 99.5f,  1200.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    std::cout << "\n--- SCENARIO 2: Flash Crash (Low Volume - Ignored by Pipeline) ---\n";
    push_tick(1, 90.0f, 400.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    std::cout << "\n--- SCENARIO 3: Flash Crash (High Volume - Circuit Breaker!) ---\n";
    push_tick(1, 80.0f, 15000.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    running.store(false, std::memory_order_release);
    engine_thread.join();

    return 0;
}
