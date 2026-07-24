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
 * ============================================================================* @file 01_hft_benchmark.cpp
 * @brief Benchmark: End-to-End Ingress Line-Rate Zero-Copy Parsing Latency
 * @details Simulated ultra-high tickrate network feed injection executing zero-copy
 * protocol demultiplexing and 4D matrix pipeline dispatching under strict clock budgets.
 */

#include <iostream>
#include <vector>
#include <array>
#include <chrono>

// Core Framework
#include "slabflux/net/demux_gateway.hpp"
#include "slabflux/core/pipeline.hpp"

// Domain Logic
#include "strategies.hpp"
#include "events.hpp"
#include "contexts.hpp"

int main() {
    std::cout << "========================================================\n";
    std::cout << " SLABFLUX - HFT LATENCY BENCHMARK \n";
    std::cout << "========================================================\n\n";

    TradingEngine alpha;
    using my_pipeline = slabflux::core::pipeline<TradingEngine>;
    my_pipeline matrix(alpha);

    slabflux::net::demux_gateway<my_pipeline> gateway;
    gateway.bind<OrderBookUpdate>();
    gateway.bind<TradeTick>();

    constexpr size_t ITERATIONS = 10'000'000;
    std::cout << "Generating " << ITERATIONS << " network packets into memory...\n";

    std::vector<std::array<char, 40>> network_feed(ITERATIONS);

    for (size_t i = 0; i < ITERATIONS; ++i) {
        char* buffer = network_feed[i].data();
        if (i % 2 == 0) {
            *reinterpret_cast<uint16_t*>(buffer) = 1001;
            auto* payload = reinterpret_cast<OrderBookUpdate*>(buffer + 8);
            payload->best_bid = 100.0 + (i % 10);
            payload->best_ask = 100.5 + (i % 10);
            payload->bid_size = 10;
            payload->ask_size = 20;
        } else {
            *reinterpret_cast<uint16_t*>(buffer) = 1002;
            auto* payload = reinterpret_cast<TradeTick*>(buffer + 8);
            payload->price = 100.25;
            payload->size = 5;
            payload->side = 1;
        }
    }

    std::cout << "Starting the benchmark...\n\n";

    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < ITERATIONS; ++i) {
        gateway.on_network_bytes_received(network_feed[i].data(), matrix);
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::nano> elapsed_ns = end - start;

    double total_ns = elapsed_ns.count();
    double ns_per_msg = total_ns / ITERATIONS;
    double msgs_per_sec = ITERATIONS / (total_ns / 1'000'000'000.0);

    std::cout << " RESULTS:\n";
    std::cout << " Processed packets:     " << alpha.total_updates_seen << "\n";
    std::cout << " Last Alpha Signal:     " << alpha.current_signal << "\n";
    std::cout << " Throughput:            " << static_cast<uint64_t>(msgs_per_sec) << " msg/sec\n";
    std::cout << " Latency per message:   " << ns_per_msg << " ns\n";

    return 0;
}
