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
#include "slabflux/compute/vector_lane_256.hpp"
#include "slabflux/compute/vector_lane_512.hpp"
#include "slabflux/compute/vector_lane_engine.hpp"
#include "slabflux/compute/kernels.hpp"

using namespace slabflux::compute;

/**
 * @brief Benchmarks the AVX2 fixed-point state engine.
 */
template <bool Shadowing>
static void BM_VectorLane256_Propagate(benchmark::State& state) {
    vector_lane_256<64, Shadowing> engine;
    int32_t signal = 512;
    uint64_t lsn = 0;

    for (auto _ : state) {
        engine.propagate(signal, ++lsn);
        benchmark::DoNotOptimize(engine);
    }
    state.SetItemsProcessed(state.iterations() * 64);
}
BENCHMARK_TEMPLATE(BM_VectorLane256_Propagate, true)->Name("BM_VectorLane256_Shadowed")->UseRealTime();
BENCHMARK_TEMPLATE(BM_VectorLane256_Propagate, false)->Name("BM_VectorLane256_Unshadowed")->UseRealTime();

/**
 * @brief Benchmarks the AVX-512 fixed-point state engine.
 */
template <bool Shadowing>
static void BM_VectorLane512_Propagate(benchmark::State& state) {
    vector_lane_512<64, Shadowing> engine;
    int32_t signal = 512;
    uint64_t lsn = 0;

    for (auto _ : state) {
        engine.propagate(signal, ++lsn);
        benchmark::DoNotOptimize(engine);
    }
    state.SetItemsProcessed(state.iterations() * 64);
}
BENCHMARK_TEMPLATE(BM_VectorLane512_Propagate, true)->Name("BM_VectorLane512_Shadowed")->UseRealTime();
BENCHMARK_TEMPLATE(BM_VectorLane512_Propagate, false)->Name("BM_VectorLane512_Unshadowed")->UseRealTime();

/**
 * @brief Benchmarks the metaprogrammed Execution Graph Vector Engine.
 */
using decay_graph = kernels::execution_graph<kernels::exponential_decay_op>;

template <bool Shadowing>
static void BM_VectorLaneEngine_Execute(benchmark::State& state) {
    vector_lane_engine<decay_graph, float, 64, Shadowing> engine;
    for (auto _ : state) {
        engine.execute();
        benchmark::DoNotOptimize(engine);
    }
    state.SetItemsProcessed(state.iterations() * 64);
}
BENCHMARK_TEMPLATE(BM_VectorLaneEngine_Execute, true)->Name("BM_VectorLaneEngine_Decay_Shadowed")->UseRealTime();
BENCHMARK_TEMPLATE(BM_VectorLaneEngine_Execute, false)->Name("BM_VectorLaneEngine_Decay_Unshadowed")->UseRealTime();

BENCHMARK_MAIN();