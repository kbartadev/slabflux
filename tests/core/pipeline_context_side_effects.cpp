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
 * ============================================================================* @file tests_context_side_effects.cpp
 * @brief Validation of SFINAE Context Resolution and Side-Effect Isolation
 */

#include <iostream>
#include <gtest/gtest.h>
#include "slabflux/core/pipeline.hpp"
#include "slabflux/pipeline/context_vault.hpp"

// ============================================================================
// TEST DOMAIN: THE SIDE-EFFECT CONTEXT
// ============================================================================

struct SideEffectCtx {
    int expected_hits = 0;
    int anomaly_hits = 0;
};

// ============================================================================
// MATRIX 1: PERFECT MATCH (Event Registered -> Handler requests Context)
// ============================================================================

class EvCtxPresent {};
REGISTER_CONTEXT(EvCtxPresent, SideEffectCtx);
EXPECT_INHERITANCE(EvCtxPresent);

class HndCtxPresent {
public:
    void on(EvCtxPresent&, SideEffectCtx& ctx) {
        ctx.expected_hits += 10;
    }
};
EXPECT_INHERITANCE(HndCtxPresent);

// ============================================================================
// MATRIX 2: CONTEXT IGNORED (Event Registered -> Handler ignores Context)
// ============================================================================

class EvCtxIgnored {};
REGISTER_CONTEXT(EvCtxIgnored, SideEffectCtx);
EXPECT_INHERITANCE(EvCtxIgnored);

class HndCtxIgnored {
public:
    static inline int static_hits = 0;

    void on(EvCtxIgnored&) {
        static_hits += 20;
    }
};
EXPECT_INHERITANCE(HndCtxIgnored);

// ============================================================================
// MATRIX 3 & 4: PURE ORPHAN & GHOST ELIMINATION
// (Event NOT Registered -> Handlers behavior check)
// ============================================================================

class EvCtxAbsent {};
EXPECT_INHERITANCE(EvCtxAbsent);

class HndCtxAbsent {
public:
    using priority = slabflux::priority<10>; // Deterministic Tie-Breaker
    static inline int static_hits = 0;

    void on(EvCtxAbsent&) {
        static_hits += 30;
    }
};
EXPECT_INHERITANCE(HndCtxAbsent);

class HndGhost {
public:
    using priority = slabflux::priority<20>; // Deterministic Tie-Breaker
    void on(EvCtxAbsent&, SideEffectCtx& ctx) {
        ctx.anomaly_hits += 999;
    }
};
EXPECT_INHERITANCE(HndGhost);

// ============================================================================
// MAIN EXECUTION THREAD
// ============================================================================

TEST(PipelineContextSideEffects, PerfectMatch) {
    {
        SideEffectCtx ctx;
        slabflux::core::pipeline<HndCtxPresent> dispatcher(HndCtxPresent{});
        EvCtxPresent event;

        dispatcher.dispatch(ctx, event);
        EXPECT_EQ(ctx.expected_hits, 10);
    }
}

TEST(PipelineContextSideEffects, ContextIgnored) {
    {
        SideEffectCtx ctx;
        HndCtxIgnored::static_hits = 0;
        slabflux::core::pipeline<HndCtxIgnored> dispatcher(HndCtxIgnored{});
        EvCtxIgnored event;

        dispatcher.dispatch(ctx, event);
        EXPECT_EQ(HndCtxIgnored::static_hits, 20);
        EXPECT_EQ(ctx.expected_hits, 0);
        EXPECT_EQ(ctx.anomaly_hits, 0);
    }
}

TEST(PipelineContextSideEffects, OrphanAndGhost) {
    {
        HndCtxAbsent::static_hits = 0;
        slabflux::core::pipeline<HndCtxAbsent, HndGhost> dispatcher(HndCtxAbsent{}, HndGhost{});
        EvCtxAbsent event;

        dispatcher.dispatch(event);
        EXPECT_EQ(HndCtxAbsent::static_hits, 30);
    }
}
