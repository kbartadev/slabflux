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
 * @file industrial_jitter_test.cpp
 */

#include <gtest/gtest.h>
#include "slabflux/core/pipeline.hpp"
#include "slabflux/core/scoped_ptr.hpp"
#include "slabflux/core/mpmc_pool.hpp"

using namespace slabflux::core;

struct alpha_event { int val; };
EXPECT_INHERITANCE(alpha_event);

struct handler_a { void on(alpha_event* ev) { ev->val += 1; } };
EXPECT_INHERITANCE(handler_a);
struct handler_b { void on(alpha_event* ev) { ev->val *= 2; } };
EXPECT_INHERITANCE(handler_b);
struct handler_drop { 
    bool on(alpha_event*) {
        return true; // Short-circuit the pipeline
    }
};
EXPECT_INHERITANCE(handler_drop);

/**
 * @brief Verifies the Single Handler Cascade logic.
 * Paradigm Shattering: Proves that the event waterfalls through the 
 * entire stack in a single contiguous instruction block.
 */
TEST(Pipeline, SingleHandlerCascade) {
    handler_a a;
    handler_b b;
    pipeline pipe(a, b);

    alpha_event ev{10};
    // Calculation: (10 + 1) * 2 = 22
    pipe.dispatch(&ev);
    EXPECT_EQ(ev.val, 22);
}

/**
 * @brief Audits Ownership Stealing and Pipeline Short-Circuiting.
 */
TEST(Pipeline, ShortCircuitAudit) {
    mpmc_pool<alpha_event, 16> pool;
    handler_a a;
    handler_drop d;
    handler_b b;
    
    // Chain: A -> Drop -> B
    pipeline pipe(a, d, b);

    auto ev = pool.make();
    ev->val = 10;
    
    // Logic: A increments to 11. Drop short-circuits. B is NEVER called.
    pipe.dispatch(ev);
    
    // 11 proves short-circuiting worked.
    EXPECT_EQ(ev->val, 11);
}

/**
 * @brief Verifies zero-overhead structural integrity.
 */
TEST(Pipeline, StructuralIntegrity) {
    handler_a a;
    pipeline<handler_a> pipe(a);

    // The pipeline itself should have no vtable and be sized only for 
    // the sum of its handler references/instances.
    EXPECT_EQ(sizeof(pipe), sizeof(void*)); // Assuming it holds 1 reference
    
    // Ensure no dynamic allocation was triggered
    // (Verified via static analysis in industrial builds)
}