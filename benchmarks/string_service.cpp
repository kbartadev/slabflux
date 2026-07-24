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
#include <string>
#include "slabflux/core/string_service.hpp"

using namespace slabflux;
using namespace slabflux::core;

/**
 * @brief Template-based Mock Pool to support large-scale allocation.
 */
template<uint32_t Slabs>
struct MockChunkPool {
    string_chunk storage[Slabs];
    uint32_t allocated{ 0 };

    string_chunk* make() {
        if (allocated >= Slabs) return nullptr;
        return &storage[allocated++];
    }
    uint32_t get_index(string_chunk* p) const { return static_cast<uint32_t>(p - storage); }
    string_chunk* get_by_index(uint32_t i) { return &storage[i]; }
    void free(string_chunk*) noexcept {}
};

/**
 * @brief Assignment Benchmark with Optimization Guards.
 * Uses ClobberMemory to prevent the compiler from skipping the work.
 */
static void BM_StringAssignment(benchmark::State& state) {
    auto pool = std::make_unique<MockChunkPool<200000>>();
    string_service<MockChunkPool<200000>> svc(*pool);

    std::string payload(state.range(0), 'A');
    fragmented_string fs{};

    for (auto _ : state) {
        // Execute the Hybrid SSO / Fragmentation logic
        svc(fs) = payload;

        // CRITICAL: Prevent compiler from eliding the code
        benchmark::DoNotOptimize(fs);
        benchmark::ClobberMemory();

        // Reset for the next iteration without timing the cleanup
        svc.clear(fs);
        pool->allocated = 0;
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
}

// Register for SKUs (32-48B) and Large Payloads (1024B)
BENCHMARK(BM_StringAssignment)->Arg(32)->Arg(48)->Arg(64)->Arg(1024)->UseRealTime();

// New benchmark for fragmented string assignment
static void BM_StringAssignment_Fragmented(benchmark::State& state) {
    auto pool = std::make_unique<MockChunkPool<200000>>();
    string_service<MockChunkPool<200000>> svc(*pool);

    // Create a very long payload that forces fragmentation (e.g., 5000 bytes)
    std::string payload(state.range(0), 'F');
    fragmented_string fs{};

    for (auto _ : state) {
        svc(fs) = payload;

        benchmark::DoNotOptimize(fs);
        benchmark::ClobberMemory();

        svc.clear(fs);
        pool->allocated = 0;
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
}
BENCHMARK(BM_StringAssignment_Fragmented)
    ->Arg(5000) // A size that guarantees fragmentation
    ->Arg(10000)
    ->UseRealTime();

// New benchmark for extracting to std::string
static void BM_StringExtraction(benchmark::State& state) {
    // Use a larger pool for fragmented strings
    auto pool = std::make_unique<MockChunkPool<200000>>();
    string_service<MockChunkPool<200000>> svc(*pool);

    std::string payload(state.range(0), 'B');
    fragmented_string fs{};
    svc(fs) = payload; // Assign once outside the loop

    for (auto _ : state) {
        std::string result = svc.extract_to_std_string(fs);
        benchmark::DoNotOptimize(result);
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
    svc.clear(fs);
}
BENCHMARK(BM_StringExtraction)->Arg(32)->Arg(48)->Arg(64)->Arg(1024)->Arg(5000)->UseRealTime();

// New benchmark for string comparison
static void BM_StringEquality(benchmark::State& state) {
    // Use a larger pool for fragmented strings
    auto pool = std::make_unique<MockChunkPool<200000>>();
    string_service<MockChunkPool<200000>> svc(*pool);

    std::string payload(state.range(0), 'C');
    fragmented_string fs{};
    svc(fs) = payload; // Assign once outside the loop

    for (auto _ : state) {
        bool eq = svc.equals(fs, payload);
        benchmark::DoNotOptimize(eq);
    }
    state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(state.range(0)));
    svc.clear(fs);
}
BENCHMARK(BM_StringEquality)->Arg(32)->Arg(48)->Arg(64)->Arg(1024)->Arg(5000)->UseRealTime();

BENCHMARK_MAIN();
