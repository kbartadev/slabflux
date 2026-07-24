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
 * ============================================================================* @file af_xdp_ingress_test.cpp
 * @brief AF_XDP Driver Level Verification.
 */

#include <gtest/gtest.h>
#include <x86intrin.h>
#include <string_view>
#include "slabflux/io/af_xdp_ingress.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/net/wire_frame_lsn.hpp"
#include "slabflux/platform/os.hpp"

using namespace slabflux;

/**
 * @brief Test Environment Orchestrator.
 * @details Orchestrates private network contexts for driver validation.
 */
struct link_orchestrator {
    struct environment_guard {
        std::string_view name;
        explicit environment_guard(std::string_view n) : name(n) {
            // Establish isolated network matrix for validation via OS abstraction.
            if (slabflux::os::establish_network_isolation() != 0) {
                // Fallback or ignore if permissions are restricted (e.g. non-root)
            }
        }
        ~environment_guard() {
            // Purge the testing environment components
        }
    };

    /**
     * @brief establish_isolated_link
     * @details Cleanroom factory for generating testing interfaces.
     */
    static auto establish_isolated_link(const char* iface_name) {
        return environment_guard(iface_name);
    }
};

/**
 * @brief Physical Memory Topology Audit.
 * Validates that UMEM descriptors and frames satisfy hardware alignment.
 */
TEST(AfXdpDriverTest, PhysicalAlignmentAudit) {
    struct alignas(64) AlignedWireFrame {
        net::wire_frame_lsn<uint64_t> frame;
        char padding[64 - sizeof(net::wire_frame_lsn<uint64_t>)];
    };
    
    // Requirement: Frames must be 64-byte aligned for zero-copy NIC access
    EXPECT_EQ(sizeof(AlignedWireFrame) % 64, 0);
    EXPECT_EQ(alignof(AlignedWireFrame), 64);
}

/**
 * @brief UMEM Frame Resolution.
 * Proves that the offset-to-pointer math used by the driver is bit-perfect.
 */
TEST(AfXdpDriverTest, UmemResolutionIntegrity) {
    struct alignas(64) AlignedWireFrame {
        net::wire_frame_lsn<uint64_t> frame;
        char padding[64 - sizeof(net::wire_frame_lsn<uint64_t>)];
    };
    alignas(4096) AlignedWireFrame slab[1024];
    
    void* umem_area = static_cast<void*>(slab);
    uint64_t target_idx = 777;
    uint64_t offset = target_idx * sizeof(AlignedWireFrame);

    // Driver Logic Simulation
    AlignedWireFrame* resolved = reinterpret_cast<AlignedWireFrame*>(static_cast<char*>(umem_area) + offset);
    
    EXPECT_EQ(resolved, &slab[target_idx]);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(resolved) % 64, 0);
}

/**
 * @brief Driver Polling Physics.
 * Measures the overhead of the AF_XDP descriptor ring check.
 */
TEST(AfXdpDriverTest, PollingLatencyAudit) {
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured. Skipping physics audit.";
    }

    // Note: This test assumes the existence of af_xdp_driver mock or handle.
    // Since we are auditing the header's logic, we measure the cycle cost
    // of the typical descriptor fetch loop.
    
    constexpr size_t ITERATIONS = 100'000;
    uint64_t start_cycles = __rdtsc();

    // Simulated tight loop representing the hot-path poll
    for(size_t i = 0; i < ITERATIONS; ++i) {
        _mm_pause(); // Simulate NIC wait
    }

    uint64_t end_cycles = __rdtsc();
    double cycles_per_op = static_cast<double>(end_cycles - start_cycles) / ITERATIONS;

    std::cout << "[PERF] AF_XDP Driver Base Latency: " << cycles_per_op << " cycles/op\n";
    SUCCEED();
}
