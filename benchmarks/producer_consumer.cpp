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
 * ============================================================================*
 * @file producer_consumer.cpp
 */

#include <benchmark/benchmark.h>
#include <thread> // Required for std::thread
#include <atomic>
#include <thread>

#include "slabflux/core.hpp"

using namespace slabflux;

struct flux_event {
    uint64_t seq;
};

static void BM_Slabflux_SPSC_Throughput(benchmark::State& state) {
    const int total = static_cast<int>(state.range(0));

    for (auto _ : state) {
        pool<flux_event, 1000000> p;
        conduit<flux_event*, 4096> c;

        std::atomic<bool> done{ false };
        std::atomic<int> consumed{ 0 };

        std::thread producer([&]() {
            for (int i = 0; i < total; ++i) {
                auto ev = p.make();
                ev->seq = i;
                while (!c.try_push(ev.get())) _mm_pause();
                ev.release();
            }
            done = true;
        });

        std::thread consumer([&]() {
            uint64_t expected = 0;
            while (!done || consumed < total) {
                if (auto ev = c.try_pop(p)) {
                    if (ev->seq != expected) {
                        state.SkipWithError("FIFO violation");
                        return;
                    }
                    expected++;
                    consumed++;
                } else {
                    _mm_pause();
                }
            }
        });

        producer.join();
        consumer.join();

        state.SetItemsProcessed(total);
    }
}


BENCHMARK(BM_Slabflux_SPSC_Throughput)
    ->Arg(1000000)
    ->Unit(benchmark::kMillisecond);
