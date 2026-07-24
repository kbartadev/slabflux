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
#include <cstring>
#include "slabflux/core.hpp"
#include "slabflux/transport/http_frame.hpp"
#include "slabflux/transport/baremetal_parser.hpp"

using namespace slabflux;

struct routed_http_event : transport::http_frame {
    static constexpr uint32_t TYPE_ID = 200;
};

struct compute_sink {
    uint64_t verified_count = 0;

    // Force inline to keep the loop body tight
    void on(routed_http_event* ev) {
        if (ev->uri.size() > 1 && ev->uri[1] == 'p') {
            verified_count++;
        }
    }
};

static void BM_Slabflux_Absolute_Lean(benchmark::State& state) {
    auto domain = std::make_unique<runtime_domain<routed_http_event>>();
    auto& pool = domain->get_pool<routed_http_event>();
    compute_sink core_logic;

    const char* raw_req = "GET /price HTTP/1.1\r\nHost: slabflux.hft\r\n\r\n";
    std::string_view req_view(raw_req);

    // WARMUP: 10k iterations to prime the Branch Target Buffer and uOp Cache
    for (int i = 0; i < 10000; ++i) {
        auto ev = pool.make();
        if (ev) {
            ev->reset();
            if (transport::baremetal_parser::parse(req_view, *ev) == transport::parser_status::OK) {
                core_logic.on(ev);
            }
        }
    }
    core_logic.verified_count = 0;

    for (auto _ : state) {
        // Step 1: O(1) Slab Acquisition
        auto ev = pool.make();

        if (ev) {
            // Step 2: High-Performance Baremetal DFA Parse
            ev->reset();
            if (transport::baremetal_parser::parse(req_view, *ev) == transport::parser_status::OK) {
                // Step 3: Verified Dispatch
                core_logic.on(ev);
            }
        }
    }

    if (core_logic.verified_count == 0) {
        state.SkipWithError("Integrity Failure: Parser output did not reach Sink.");
    }

    // If we missed even one iteration, the benchmark is invalid.
    if (core_logic.verified_count != static_cast<uint64_t>(state.iterations())) {
        state.SkipWithError("Integrity Failure: Verified count mismatch.");
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_Slabflux_Absolute_Lean)->Unit(benchmark::kNanosecond);
