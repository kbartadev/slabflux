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
 * ============================================================================@file tests_production_guard.cpp
 @brief Hardcore Production Safety & Lifecycle Propagation Validation
*/

#define SLABFLUX_SKIP_META_GENERATION
#include <iostream>
#include <gtest/gtest.h>
#include "slabflux/core/pipeline.hpp"
#include "slabflux/pipeline/context_vault.hpp"

// ============================================================================
// TEST 1: ABSTRACT BASE RESILIENCE
// Prevents compilation crashes when the DAG forces evaluation of pure virtuals.
// ============================================================================

struct AbsCtx { int val = 0; };

class EvAbs {};
REGISTER_CONTEXT(EvAbs, AbsCtx);
EXPECT_INHERITANCE(EvAbs);

// Pure abstract handler in the DAG
class HndAbsBase {
public:
    using priority = slabflux::priority<10>;
    virtual void on(EvAbs&, AbsCtx& ctx) = 0;
};
EXPECT_INHERITANCE(HndAbsBase);

class HndAbsImpl : public HndAbsBase {
public:
    using priority = slabflux::priority<20>;
    void on(EvAbs&, AbsCtx& ctx) override {
        ctx.val = 777;
    }
};
EXPECT_INHERITANCE(HndAbsImpl, HndAbsBase);

// ============================================================================
// TEST 2: SYNCHRONOUS STATE LIFECYCLE
// Validates guaranteed execution order for multi-stage procedural pipelines.
// ============================================================================

struct LifeCtx { int state = 0; };

class EvLifecycle {};
REGISTER_CONTEXT(EvLifecycle, LifeCtx);
EXPECT_INHERITANCE(EvLifecycle);

class HndLifeStart {
public:
    using priority = slabflux::priority<10>;
    void on(EvLifecycle&, LifeCtx& ctx) {
        ctx.state += 5; // 0 + 5 = 5
    }
};
EXPECT_INHERITANCE(HndLifeStart);

class HndLifeMid : public HndLifeStart {
public:
    using priority = slabflux::priority<20>;
    void on(EvLifecycle&, LifeCtx& ctx) {
        ctx.state *= 10; // 5 * 10 = 50
    }
};
EXPECT_INHERITANCE(HndLifeMid, HndLifeStart);

class HndLifeEnd : public HndLifeMid {
public:
    using priority = slabflux::priority<30>;
    void on(EvLifecycle&, LifeCtx& ctx) {
        ctx.state += 9; // 50 + 9 = 59
    }
};
EXPECT_INHERITANCE(HndLifeEnd, HndLifeMid);

// ============================================================================
// MAIN EXECUTION THREAD
// ============================================================================

TEST(PipelineProductionGuard, AbstractResilience) {
    {
        AbsCtx ctx;
        slabflux::core::pipeline<HndAbsImpl> dispatcher(HndAbsImpl{});
        EvAbs eventInstance;

        dispatcher.dispatch(ctx, eventInstance);
        EXPECT_EQ(ctx.val, 777);
    }
}

TEST(PipelineProductionGuard, LifecycleIntegrity) {
    {
        LifeCtx ctx;
        slabflux::core::pipeline<HndLifeEnd> dispatcher(HndLifeEnd{});
        EvLifecycle eventInstance;

        dispatcher.dispatch(ctx, eventInstance);
        EXPECT_EQ(ctx.state, 59);
    }
}
