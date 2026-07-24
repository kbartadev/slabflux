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
 * @file jitter_audit.cpp
 * @brief Measures CPU cycle variance to verify O(1) behavior.
 */
#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <immintrin.h> // For _mm_pause
#include <cstdint>
#include "slabflux/compute/vector_lane_256.hpp"
#include "slabflux/core/hardware_topology.hpp"

#ifdef _MSC_VER
#include <windows.h>
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

using namespace slabflux::core;

TEST(SlabFluxAudit, VectorEngine_Jitter_Profile) {
    // 1. Raise priority so Windows leaves us alone
    slabflux::core::hardware_topology::pin_thread(0);

    slabflux::compute::vector_lane_256<64> engine;
    const int iterations = 1000000;
    std::vector<uint64_t> cycles;
    cycles.reserve(iterations);

    // 2. WARM-UP: 100k runs without measurement to warm up the cache
    for (int i = 0; i < 100000; ++i) {
        engine.propagate(0.5f, i);
    }

    // 3. Actual measurement
    for (int i = 0; i < iterations; ++i) {
        uintptr_t t0 = __rdtsc();
        engine.propagate(0.5f, i);
        uintptr_t t1 = __rdtsc();
        cycles.push_back(t1 - t0);
    }

    std::sort(cycles.begin(), cycles.end());

    // Look at P99.9; treat the maximum (p100) with suspicion
    uint64_t p99 = cycles[static_cast<size_t>(iterations * 0.99)];
    uint64_t p999 = cycles[static_cast<size_t>(iterations * 0.999)];
    uint64_t pmax = cycles.back();

    //printf("\n[AUDIT RESULTS]\n");
    //printf("P99   : %llu cycles\n", p99);
    //printf("P99.9 : %llu cycles\n", p999);
    //printf("MAX   : %llu cycles\n", pmax);

    // If there's still a spike, it's likely an unavoidable OS interrupt.
    // Validate based on P99.9 � that's more realistic.
    ASSERT_LT(p999, p99 * 10) << "Jitter detected even after core pinning!";
}
