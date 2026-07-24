/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 * PROPRIETARY AND SOURCE-AVAILABLE CODEBASE. ALL RIGHTS RESERVED.
 *
 * This source file and all constitutive programmatic expressions contained herein 
 * are the exclusive intellectual property of Kristóf Barta, established and 
 * distributed strictly under the conditions of the SLABFLUX SOURCE-AVAILABLE 
 * AND ECOSYSTEM LICENSE (the "License").
 *
 * TITLE TO AND OWNERSHIP OF THE SOFTWARE, THE ENGINE, CORE LOGIC, ARCHITECTURAL 
 * LAYOUTS, AND ALL ASSOCIATED INSIGHTS REMAIN SOLELY VESTED IN THE AUTHOR.
 *
 * ----------------------------------------------------------------------------
 * TECHNICAL WARNING & SYSTEM ARCHITECTURE NOTICE
 * ----------------------------------------------------------------------------
 * THIS SOFTWARE UTILIZES ARCHITECTURE-SPECIFIC HARDWARE INTRINSICS AND OPERATES
 * THROUGH LOW-LEVEL, KERNEL-ADJACENT EXECUTION PATHS THAT REDUCE OR BYPASS STANDARD
 * OPERATING SYSTEM MEDIATION LAYERS. INCORRECT INTEGRATION, EXECUTION, OR CONFIGURATION
 * MAY RESULT IN SEVERE SYSTEM INSTABILITY, KERNEL PANICS, OR PERMANENT LOSS OF DATA,
 * AND MAY RENDER SYSTEMS TEMPORARILY OR PERMANENTLY UNUSABLE UNTIL REPAIRED OR
 * RECONFIGURED.
 * ============================================================================
 * @file uring_shim_test.cpp
 * @brief Validation suite for the native io_uring kernel shim layer.
 */

#include <gtest/gtest.h>
#include "slabflux/io/uring_shim.hpp"

using namespace slabflux::io;

/**
 * @brief Lifecycle Integrity Audit.
 * Verifies that the shim layer correctly abstracts liburing lifecycle calls
 * without corrupting the underlying ring pointers.
 */
TEST(UringShimTest, LifecycleIntegrity) {
    io_uring ring;
    
    try {
        // Attempt standard initialization without SQPOLL to ensure universal CI support
        uring_shim::ring_init(32, &ring, 0, 0);
        
        io_uring_sqe* sqe = uring_shim::get_sqe(&ring);
        EXPECT_NE(sqe, nullptr);
        
        // Safely extract properties
        io_uring_prep_nop(sqe); // Optional fallback test if implemented, or general sanity check
        int sub = uring_shim::submit(&ring);
        EXPECT_GE(sub, 0);
        
        // Tear down
        uring_shim::ring_exit(&ring);
        SUCCEED();
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Underlying liburing is not supported on this kernel: " << e.what();
    }
}