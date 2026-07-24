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
#include <string>
#include <charconv> // For std::from_chars
#include "slabflux/io/simd_parser.hpp"

using namespace slabflux;

// Test data for integer parsing
static const char* FOUR_DIGIT_NUMBER = "1234";
static const char* LONG_NUMBER = "9876543210";

// Benchmark for SIMD fast_atoi_4
static void BM_SimdParser_FastAtoi4(benchmark::State& state) {
    for (auto _ : state) {
        uint32_t result = io::simd_parser::fast_atoi_4(FOUR_DIGIT_NUMBER);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SimdParser_FastAtoi4)->UseRealTime();

// Benchmark for std::from_chars (C++17)
static void BM_StdFromChars_Atoi4(benchmark::State& state) {
    for (auto _ : state) {
        uint32_t result;
        std::from_chars(FOUR_DIGIT_NUMBER, FOUR_DIGIT_NUMBER + 4, result);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_StdFromChars_Atoi4)->UseRealTime();

// Benchmark for std::stoi (general purpose, might be slower)
static void BM_StdStoi_Atoi4(benchmark::State& state) {
    std::string s(FOUR_DIGIT_NUMBER);
    for (auto _ : state) {
        int result = std::stoi(s);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_StdStoi_Atoi4)->UseRealTime();

// Benchmark for std::from_chars with a longer number (to show overhead of non-fixed size)
static void BM_StdFromChars_LongAtoi(benchmark::State& state) {
    for (auto _ : state) {
        uint64_t result;
        std::from_chars(LONG_NUMBER, LONG_NUMBER + std::strlen(LONG_NUMBER), result);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_StdFromChars_LongAtoi)->UseRealTime();

// Benchmark for std::stoi with a longer number
static void BM_StdStoi_LongAtoi(benchmark::State& state) {
    std::string s(LONG_NUMBER);
    for (auto _ : state) {
        long long result = std::stoll(s);
        benchmark::DoNotOptimize(result);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_StdStoi_LongAtoi)->UseRealTime();

BENCHMARK_MAIN();