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
 * ============================================================================* @file 04_ultimate_logic_synthetis.cpp
 * @brief Enterprise HFT Matrix: Inheritance, Interfaces, Concepts & Mutation.
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <thread>
#include <atomic>
#include <immintrin.h>
#include "slabflux/core.hpp"

using namespace slabflux;
using namespace slabflux::core;

// ============================================================================
// 1. THE VERTICAL MATRIX: Inheritance and Interface Layers
// ============================================================================

// L0: Network Metadata (Hardware layer)
struct wire_meta {
    uint64_t    capture_ns;
    std::string ip_address;
};

// L1: Regulatory Interface (Pure virtual Base)
struct compliance_interface {
    virtual ~compliance_interface() = default;
    virtual bool is_kyc_cleared() const noexcept = 0;
};

// L2: The Final HFT Event (Native C++ Multiple Inheritance)
struct direct_market_order : wire_meta, compliance_interface {
    uint32_t order_id;
    double   price;
    double   quantity;
    bool     kyc_approved;

    direct_market_order(const char* ip, uint32_t id, double p, double q, bool kyc)
        : order_id(id), price(p), quantity(q), kyc_approved(kyc) {
        this->capture_ns = 1682390400000000000ULL;
        this->ip_address = ip;
    }

    // Implementing the abstract interface
    bool is_kyc_cleared() const noexcept override {
        return kyc_approved;
    }
};

// ============================================================================
// 2. CONCEPTS: Structural Discovery
// ============================================================================

template <typename T>
concept HasExposure = requires(T a) { a.price; a.quantity; };

// ============================================================================
// 3. THE PIPELINE HANDLERS: 5 Different Engineering Patterns
// ============================================================================

// Stage 1: Latency Monitor (Reads only the network layer – CONST)
struct latency_monitor {
    void on(const wire_meta& ev) {
        std::cout << "[L0 INFRA] Packet from " << ev.ip_address
                  << " | TS: " << ev.capture_ns << " ns\n";
    }
};

// Stage 2: Compliance Auditor (Abstract interface – BOOL SHORT‑CIRCUIT)
struct compliance_auditor {
    bool on(const compliance_interface& ev) {
        if (!ev.is_kyc_cleared()) {
            std::cout << "[L1 COMPLIANCE] REJECTED: KYC validation failed.\n";
            return false; // Short-circuits the pipeline
        }
        std::cout << "[L1 COMPLIANCE] APPROVED: KYC valid.\n";
        return true;
    }
};

// Stage 3: Risk Gatekeeper (Concept‑based filtering – DUCK‑TYPING)
struct risk_gatekeeper {
    template <HasExposure E>
    bool on(const E& ev) {
        double exposure = ev.price * ev.quantity;
        if (exposure > 1'000'000.0) {
            std::cout << "[L2 RISK] REJECTED: Exposure too high ($"
                      << std::fixed << std::setprecision(2) << exposure << ")\n";
            return false;
        }
        std::cout << "[L2 RISK] APPROVED: Exposure within limits.\n";
        return true;
    }
};

// Stage 4: Accounting (Runtime Mutation – NON‑CONST REFERENCE)
struct accounting_processor {
    void on(direct_market_order& ev) {
        double exchange_fee = ev.quantity * 0.01;
        ev.price += exchange_fee; // Mutate price directly!
        std::cout << "[L2 ACCOUNTING] Added exchange fee. New routing price: $"
                  << ev.price << "\n";
    }
};

// Stage 5: Execution Engine (The leaf executor)
struct execution_engine {
    void on(const direct_market_order& ev) {
        std::cout << "[L2 EXECUTION] Routing Order #" << ev.order_id
                  << " to matching engine.\n";
    }
};

// ============================================================================
// 4. THE ORCHESTRATION
// ============================================================================

int main() {
    std::cout << "=== Conduit Ultimate Logic Synthesis ===\n\n";

    // 1. Core Engine Primitives
    spsc_pool<direct_market_order, 256> engine_pool;
    spsc_conduit<direct_market_order*, 1024> bus;

    // 2. Instantiate handlers
    latency_monitor       infra;
    compliance_auditor    auditor;
    risk_gatekeeper       risk;
    accounting_processor  accounting;
    execution_engine      exec;

    // 3. Define the pipeline
    pipeline<latency_monitor, compliance_auditor, risk_gatekeeper,
             accounting_processor, execution_engine>
        matrix_pipe(infra, auditor, risk, accounting, exec);

    std::atomic<bool> running{ true };

    // --- CONSUMER THREAD (The background Engine) ---
    std::thread engine_thread([&]() {
        // Combined Hot-Path & Final Drain spin loop
        while (true) {
            // try_pop() returns an RAII guard. When the scope ends, it cleans up.
            if (auto ev = bus.try_pop(engine_pool)) {
                // dispatch() automatically resolves references for the handlers!
                matrix_pipe.dispatch(*ev);
            } else if (!running.load(std::memory_order_relaxed)) {
                break; // Cold-path exit (Conduit drained and shutdown requested)
            } else {
                _mm_pause(); // Cold-path wait
            }
        }
    });

    // --- PRODUCER HELPER (Safe ingress) ---
    auto push_order = [&](const char* ip, uint32_t id, double p, double q, bool kyc) {
        auto ev = engine_pool.make(ip, id, p, q, kyc);
        while (!ev) {
            std::this_thread::yield();
            ev = engine_pool.make(ip, id, p, q, kyc);
        }
        while (!bus.try_push(ev.release())) _mm_pause();
    };

    // --- EXECUTE SCENARIOS ---
    std::cout << "--- SCENARIO 1: Valid Retail Order ---\n";
    push_order("10.0.1.55", 1001, 150.0, 100.0, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    std::cout << "\n--- SCENARIO 2: Toxic Whale Order (Risk Breach) ---\n";
    push_order("10.0.1.99", 1002, 200.0, 10000.0, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    std::cout << "\n--- SCENARIO 3: Illegal Order (KYC Failed) ---\n";
    push_order("192.168.1.1", 1003, 50.0, 10.0, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    // --- GRACEFUL SHUTDOWN ---
    running.store(false, std::memory_order_release);
    engine_thread.join();

    return 0;
}
