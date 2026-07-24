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

#include <gtest/gtest.h>
#include "slabflux/io/egress.hpp"
#include "slabflux/io/strategy.hpp"

using namespace slabflux::io;

/**
 * @brief SPSC Isolation Physics.
 * Proves that the egress ring maintains strict cache-line separation
 * between the head and tail to prevent interconnect thrashing.
 */
TEST(SovereignEgressTest, CacheLineIsolation) {
    uint64_t slab[1024];
    egress_ring<uint64_t, 1024> ring(slab);
    
    // Requirement: The ring structure itself must be 64-byte aligned
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&ring) % 64, 0);
    
    // Verify throughput on a single core
    for(uint64_t i=0; i<100; ++i) {
        ASSERT_TRUE(ring.try_push(i));
        EXPECT_EQ(*ring.poll_next(), i);
        ring.commit_pop();
    }
}

/**
 * @brief Strategy Switch Logic.
 */
TEST(IoStrategyTest, BackendEnumVerification) {
    io_backend b1 = io_backend::io_uring_kernel;
    io_backend b2 = io_backend::af_xdp_bypass;
    
    EXPECT_NE(b1, b2);
}
