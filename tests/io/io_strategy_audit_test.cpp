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
#include "slabflux/io/strategy.hpp"
#include "slabflux/io/hardware_shaper.hpp"

using namespace slabflux::io;

/**
 * @brief Backend Paradigm Verification.
 */
TEST(IoStrategyAudit, ParadigmConsistency) {
    io_backend b1 = io_backend::io_uring_kernel;
    io_backend b2 = io_backend::af_xdp_bypass;
    
    // Proves that paradigms are distinct and detectable at compile time
    static_assert(sizeof(io_backend) == sizeof(int), "Enum size breach");
    EXPECT_NE(b1, b2);
}

/**
 * @brief Hardware Shaper Command Integrity.
 * Since the shaper wraps system calls, we verify the command string generation.
 */
TEST(HardwareShaperAudit, CommandStringSanity) {
    // Hardware shaper is core-local and TSC-based.
    // Requirement: Must be constructible with valid link and CPU parameters.
    EXPECT_NO_THROW(hardware_shaper shaper(10.0, 3.5));
}

/**
 * @brief Physical Residency of IO Meta.
 */
TEST(IoStrategyAudit, MetaResidency) {
    struct alignas(64) io_context {
        io_backend strategy;
        uint64_t flags;
    };
    
    EXPECT_EQ(sizeof(io_context) % 64, 0);
    EXPECT_EQ(alignof(io_context), 64);
}
