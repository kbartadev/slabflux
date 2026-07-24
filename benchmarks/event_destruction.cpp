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
 * ============================================================================*/

#include <benchmark/benchmark.h>
#include <memory>
#include "slabflux/core.hpp"

using namespace slabflux;

// Fix: Use the actual library base template.
// allocated_event<Derived, RoutingID> is the base layer in core.hpp.
struct destruction_test_event {
    uint64_t payload[8];
};

static void BM_Slabflux_Lifecycle_Full(benchmark::State& state) {
    // runtime_domain manages the pools for the specified event types
    auto domain = std::make_unique<runtime_domain<destruction_test_event>>();
    auto& pool = domain->get_pool<destruction_test_event>();

    for (auto _ : state) {
        // pool.make() returns an event_ptr<T>
        auto* ev = pool.make_raw();
        benchmark::DoNotOptimize(ev);
        pool.release(ev);
    }
}

static void BM_Slabflux_Acquisition_Only(benchmark::State& state) {
    auto domain = std::make_unique<runtime_domain<destruction_test_event>>();
    auto& pool = domain->get_pool<destruction_test_event>();

    for (auto _ : state) {
        auto* ev = pool.make_raw();
        benchmark::DoNotOptimize(ev);

        // ev.release() sets the internal deleter_ to nullptr.
        // This bypasses the release_to_pool call in ~event_ptr().
        // No longer needed: now explicitly releasing raw pointer
        // ev.release();
    }
}

BENCHMARK(BM_Slabflux_Lifecycle_Full)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Slabflux_Acquisition_Only)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
