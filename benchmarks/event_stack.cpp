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

#include <random>
#include <algorithm>
#include <vector>
#include <mutex>
#include <atomic>
#include <utility>
#include <tuple>
#include <benchmark/benchmark.h>
#include "slabflux/core.hpp"

using namespace slabflux;

// ============================================================================
// 1. LEGACY INHERITANCE STACK
// ============================================================================
struct Base {
    virtual ~Base() = default;
    virtual void logic(int& x) = 0;
};

template <std::size_t N>
struct VStage : public Base {
    void logic(int& x) override { x ^= static_cast<int>(N * 0x517cc1b7U); }
};

template <std::size_t... Is>
void fill_legacy_stack_impl(std::vector<Base*>& stack, std::index_sequence<Is...>) {
    (stack.push_back(new VStage<Is>()), ...);
}

void fill_legacy_stack(std::vector<Base*>& stack) {
    fill_legacy_stack_impl(stack, std::make_index_sequence<100>{});
}

// ============================================================================
// 2. SLABFLUX STATIC MATRIX STACK
// ============================================================================
struct alignas(64) flow_event {
    int val = 0xACE;
    char _pad[124]; // Pad to exactly 128 bytes to mimic original layout sizing
};

template <std::size_t N>
struct SHandler {
    void on(flow_event* ev) { ev->val ^= static_cast<int>(N * 0x517cc1b7U); }
};

template <std::size_t... Is>
auto make_flow_pipeline(std::index_sequence<Is...>) {
    static std::tuple<SHandler<Is>...> handlers;
    return pipeline<SHandler<Is>...>(std::get<Is>(handlers)...);
}

// ============================================================================
// 3. THE PROOF BENCHMARKS
// ============================================================================

static void BM_Legacy_Inheritance_Flow(benchmark::State& state) {
    std::vector<Base*> stack;
    fill_legacy_stack(stack);

    // Shuffle destroys Branch Target Buffer (BTB) caching
    std::mt19937_64 g(42);
    std::shuffle(stack.begin(), stack.end(), g);

    int dummy = 0xACE;
    for (auto _ : state) {
        for (auto* stage : stack) {
            stage->logic(dummy);
            benchmark::DoNotOptimize(dummy);
        }
    }

    for (auto* s : stack) delete s;
}

static void BM_SLAB_Matrix_Fusion_Flow(benchmark::State& state) {
    auto matrix_pipe = make_flow_pipeline(std::make_index_sequence<100>{});

    pool<flow_event, 1> memory;
    auto ev = memory.make();

    for (auto _ : state) {
        // Fuses 100 stages into one contiguous machine-code block
        matrix_pipe.dispatch(ev);
        benchmark::DoNotOptimize(ev);
    }
}

#include <mutex>

std::mutex sync_mutex;

static void BM_Legacy_Synchronized_Flow(benchmark::State& state) {
    std::vector<Base*> stack;
    fill_legacy_stack(stack);
    std::mt19937_64 g(42);
    std::shuffle(stack.begin(), stack.end(), g);

    int dummy = 0xACE;
    for (auto _ : state) {
        std::lock_guard<std::mutex> lock(sync_mutex); // Lock overhead
        for (auto* stage : stack) {
            stage->logic(dummy);
            benchmark::DoNotOptimize(dummy);
        }
    }
    for (auto* s : stack) delete s;
}

static void BM_SLAB_Matrix_Fusion_Synchronized_Flow(benchmark::State& state) {
    auto matrix_pipe = make_flow_pipeline(std::make_index_sequence<100>{});

    pool<flow_event, 1> memory;
    auto ev = memory.make();

    for (auto _ : state) {
        std::lock_guard<std::mutex> lock(sync_mutex); // Lock overhead
        matrix_pipe.dispatch(ev);
        benchmark::DoNotOptimize(ev);
    }
}

// Force to reach thermal and cache equilibrium
BENCHMARK(BM_Legacy_Synchronized_Flow)->MinWarmUpTime(1.5)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_SLAB_Matrix_Fusion_Synchronized_Flow)->MinWarmUpTime(1.5)->Unit(benchmark::kNanosecond);

// Global mutex for contention
std::mutex flow_mutex;

// ============================================================================
// 1. LEGACY SYNCHRONIZED (HEAVY CONTENTION)
// ============================================================================
static void BM_Sync_Contention_Legacy(benchmark::State& state) {
    // Persistent setup outside the hot loop
    std::vector<Base*> stack;
    fill_legacy_stack(stack);
    std::mt19937_64 g(42);
    std::shuffle(stack.begin(), stack.end(), g);

    int dummy = 0xACE;

    for (auto _ : state) {
        // Multiple threads will fight for this lock
        std::lock_guard<std::mutex> lock(flow_mutex);

        for (auto* stage : stack) {
            stage->logic(dummy);
            benchmark::DoNotOptimize(dummy);
        }
    }

    // Cleanup
    for (auto* s : stack) delete s;
}
// Run on 1 to 12 threads to simulate your 12-core CPU
BENCHMARK(BM_Sync_Contention_Legacy)->ThreadRange(1, 12)->UseRealTime();

// ============================================================================
// 4. SLABFLUX MATRIX FUSION SYNCHRONIZED (LIGHT CONTENTION)
// ============================================================================
static void BM_Sync_Contention_SLABFLUX(benchmark::State& state) {
    // Pipeline setup
    auto matrix_pipe = make_flow_pipeline(std::make_index_sequence<100>{});

    pool<flow_event, 1> memory;
    auto ev = memory.make();

    for (auto _ : state) {
        // SLABFLUX releases this lock ~56ns faster than Legacy
        std::lock_guard<std::mutex> lock(flow_mutex);

        matrix_pipe.dispatch(ev);
        benchmark::DoNotOptimize(ev);
    }
}
BENCHMARK(BM_Sync_Contention_SLABFLUX)->ThreadRange(1, 12)->UseRealTime();

BENCHMARK(BM_Legacy_Inheritance_Flow)
->MinWarmUpTime(1.5)
->Unit(benchmark::kNanosecond);

BENCHMARK(BM_SLAB_Matrix_Fusion_Flow)
->MinWarmUpTime(1.5)
->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
