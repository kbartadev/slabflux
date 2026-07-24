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
 * ============================================================================* @file 01_lockfree_spsc_bus.cpp
 * @brief Cross-thread telemetry processing using lock-free SPSC Conduits.
 */
#include <iostream>
#include <thread>
#include "slabflux/meta.hpp"
#include "slabflux/core/pool.hpp"
#include "slabflux/core/conduit.hpp"
#include "slabflux/core/event_gateway.hpp"

using namespace slabflux;
using namespace slabflux::core;

struct telemetry_event {
    int sensor_id;
    double value;
    telemetry_event(int id, double v) : sensor_id(id), value(v) {}
};

int main() {
    std::cout << "=== Showcase 03: Lock-Free SPSC Bus ===\n\n";

    // O(1) LIFO allocator with lock-free MPSC return wire
    spsc_pool<telemetry_event, 1024> pool; // Assuming spsc_pool now returns T*

    // SPSC (Single-Producer Single-Consumer) lock-free conduit with capacity 1024.
    // Designed for contention-free transfer between single producer / single consumer threads.
    spsc_ring_conduit<telemetry_event*, 1024> hardware_bus;

    // NOTE: Gateway needs to be updated to work with raw pointers T*
    // This might involve changes in how it calls publish/consume or how it handles memory.
    // For now, assuming gateway can be updated to use T* implicitly or explicitly.
    event_gateway gateway(pool, hardware_bus);

    // Consumer thread.
    std::thread background_processor([&]() {
        bool running = true;
        while (running) {
            // gateway.consume needs to handle raw pointers T*
            // Assuming it either internally dereferences or passes T* to the lambda.
            gateway.consume<telemetry_event>([&](const auto& ref) { // Changed lambda arg to T*
                if (ref->sensor_id == -1) running = false;
                else std::cout << "[Thread] Received value: " << ref->value << "\n";
            });
        }
        std::cout << "[Thread] Shutting down.\n";
    });

    // Producer thread (main thread).
    std::cout << "[Main] Generating telemetry...\n";

    // Using the supplementary 'push' for ergonomic injection
    // gateway.publish<telemetry_event> now likely pushes T*
    gateway.publish<telemetry_event>(1, 42.5);
    gateway.publish<telemetry_event>(2, 99.9);
    gateway.publish<telemetry_event>(-1, 0.0); // Poison pill.

    background_processor.join();
    return 0;
}
