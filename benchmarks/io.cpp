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
#include <cstring>
#include <string_view>

#include "slabflux/io/header_parser.hpp"
#include "slabflux/io/simd_parser.hpp"
#include "slabflux/io/stack.hpp"
#include "slabflux/io/durable_journal.hpp"
#include "slabflux/io/hardware_shaper.hpp"

using namespace slabflux::io;

/**
 * @brief Benchmarks the AVX2-accelerated header parser.
 */
static void BM_header_parsing(benchmark::State& state) {
    const std::string_view request = 
        "GET /api/v1/market/ticker HTTP/1.1\r\n"
        "Host: api.slabflux.io\r\n"
        "User-Agent: SlabFlux-Bench/2.0\r\n"
        "Accept: application/json\r\n"
        "X-Slab-Priority: High\r\n"
        "X-Request-ID: 550e8400-e29b-41d4-a716-446655440000\r\n"
        "\r\n";
    header_parser::parsed_event ev;
    for (auto _ : state) {
        header_parser::parse_fast(request.data(), request.data() + request.size(), &ev);
        ev.header_count = 0;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_header_parsing);

/**
 * @brief Benchmarks low-level SIMD primitives.
 */
static void BM_simd_delimiter(benchmark::State& state) {
    alignas(64) char buffer[64];
    std::memset(buffer, 'X', 64);
    buffer[31] = ':';
    for (auto _ : state) {
        benchmark::DoNotOptimize(simd_parser::find_delimiter(buffer, ':'));
    }
}
BENCHMARK(BM_simd_delimiter);

static void BM_simd_atoi(benchmark::State& state) {
    const char* num = "8888";
    for (auto _ : state) {
        benchmark::DoNotOptimize(simd_parser::fast_atoi_4(num));
    }
}
BENCHMARK(BM_simd_atoi);

/**
 * @brief Benchmarks the minimal network stack responder overhead.
 */
static void BM_network_stack_icmp(benchmark::State& state) {
    alignas(64) uint8_t rx_frame[128];
    alignas(64) uint8_t tx_buf[128];
    std::memset(rx_frame, 0, 128);
    rx_frame[12] = 0x08; rx_frame[13] = 0x00;
    rx_frame[23] = 1;
    rx_frame[14] = 0x45;
    rx_frame[14+20] = 8;
    stack s;
    for (auto _ : state) {
        s.handle_infrastructure_traffic(rx_frame, tx_buf);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_network_stack_icmp);

/**
 * @brief Benchmarks the O(1) persistent journaling reservation.
 */
static void BM_journaling_reserve(benchmark::State& state) {
    struct benchmark_event { uint64_t data[8]; };
    durable_journal<benchmark_event> journal("/tmp/slabflux_bench.journal");
    for (auto _ : state) {
        auto* slot = journal.reserve_slot();
        if (__builtin_expect(slot != nullptr, 1)) {
            journal.commit_slot();
        } else {
            journal.reset();
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_journaling_reserve);

BENCHMARK_MAIN();