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
/* tests/bridge/shared_state_buffer_test.cpp */
#include <gtest/gtest.h>
#include <cstdint>
#include <atomic>
#include "slabflux/bridge/shared_state_buffer.hpp"

// Check for numa presence flags safely based on standard system architecture mapping
#if __has_include(<numa.h>)
#include <numa.h>
#define SLABFLUX_HAS_NUMA_SYS_FLAGS 1
#else
#define SLABFLUX_HAS_NUMA_SYS_FLAGS 0
#endif

using namespace slabflux::bridge;

TEST(SharedStateBufferTest, NUMAAllocAndStateSync) {
    #if SLABFLUX_HAS_NUMA_SYS_FLAGS
    // ADR 005 & 006 Compliance Validation Guard Track
    // Skip execution if running on single-socket hardware where remote NUMA mapping nodes do not exist
    if (numa_available() < 0 || numa_max_node() < 1) {
        GTEST_SKIP() << "Skipping: Physical CPU topology does not feature multi-socket remote NUMA domains.";
    }

    auto slab = shared_state_slab<float, 8>::create();

    // Assert cache-aligned allocation matching ADR 007 requirements
    EXPECT_EQ(reinterpret_cast<uintptr_t>(slab) % 64, 0);

    // Verify atomicity of LSN updates across the memory bus boundary
    slab->published_lsn.store(100);
    EXPECT_EQ(slab->published_lsn.load(std::memory_order_acquire), 100);

    delete slab;
    #else
    GTEST_SKIP() << "Skipping: Architecture compiled without native libnuma development flags.";
    #endif
}

struct MockState {
    int value;
};

TEST(SharedStateBufferTest, Layout) {
    // Verify that the shared state slab adheres to strict cache line alignment rules (ADR 007)
    EXPECT_EQ(alignof(shared_state_slab<MockState, 16>), 64);
}

TEST(SharedStateBufferTest, lsn_tracking) {
    // Secure validation tracking without triggering physical numa_alloc dependencies
    shared_state_slab<MockState, 4> slab;
    slab.published_lsn.store(100);
    EXPECT_EQ(slab.published_lsn.load(), 100);
}
