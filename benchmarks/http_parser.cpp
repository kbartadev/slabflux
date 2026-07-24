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
#include "slabflux/transport/http_frame.hpp"
#include "slabflux/transport/baremetal_parser.hpp"
#include "slabflux/core.hpp"

using namespace slabflux;
using namespace slabflux::transport;

static const char* RAW_HTTP =
"GET /price?symbol=AAPL HTTP/1.1\r\n"
"Host: example.com\r\n"
"User-Agent: SLABFLUX-Bench\r\n"
"Accept: */*\r\n"
"\r\n";

static void BM_HttpParser_Throughput(benchmark::State& state) {
    const char* data = RAW_HTTP;
    const size_t len = std::strlen(RAW_HTTP);
    std::string_view req_view(data, len);

    // Persistent event state to avoid constructor/destructor noise
    http_frame evt;

    // PRE-FETCH: Warm the L1 Instruction cache (32 KiB) before timing
    for (int i = 0; i < 100; ++i) {
        evt.reset();
        (void)baremetal_parser::parse(req_view, evt);
    }

    for (auto _ : state) {
        // STEP 1: Direct DFA Execution
        evt.reset();
        if (baremetal_parser::parse(req_view, evt) == parser_status::OK) {
            // STEP 2: Force the compiler to acknowledge the result without side effects
            benchmark::DoNotOptimize(evt.method);
            benchmark::DoNotOptimize(evt.uri);
        }
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_HttpParser_Throughput)->Unit(benchmark::kNanosecond);