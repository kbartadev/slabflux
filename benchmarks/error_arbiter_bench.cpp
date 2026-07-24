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
#include "slabflux/rte/error_arbiter.hpp"

using namespace slabflux::rte;

/**
 * @brief Measures the hot-path latency of recording a standard error/warning.
 * Does not trigger any escalation paths.
 */
static void BM_ErrorArbiter_Record_Warning(benchmark::State& state) {
    error_arbiter<65536> arbiter;
    uint64_t lsn = 0;
    for (auto _ : state) {
        arbiter.record_error(error_domain::compute, 0x100, error_severity::warning, lsn++);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ErrorArbiter_Record_Warning)->UseRealTime();

/**
 * @brief Measures the overhead of an error triggering the escalation callback.
 */
static void empty_escalation_handler(const error_record& rec) {
    benchmark::DoNotOptimize(const_cast<error_record&>(rec));
}

static void BM_ErrorArbiter_Record_Escalation(benchmark::State& state) {
    error_arbiter<65536> arbiter;
    arbiter.set_escalation_policy(error_severity::critical, empty_escalation_handler);
    
    uint64_t lsn = 0;
    for (auto _ : state) {
        // Triggers the escalation policy set above
        arbiter.record_error(error_domain::network, 0x503, error_severity::critical, lsn++);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ErrorArbiter_Record_Escalation)->UseRealTime();

BENCHMARK_MAIN();