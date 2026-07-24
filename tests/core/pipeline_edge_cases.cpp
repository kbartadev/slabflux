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
 * ============================================================================* @file tests_edge_cases.cpp
 * @brief Extreme structural and behavioral edge cases for SlabFlux
 */

#include <iostream>
#include <gtest/gtest.h>
#include "slabflux/core/pipeline.hpp"
#include "slabflux/pipeline/context_vault.hpp"

// ============================================================================
// EDGE CASE 1: CONTEXT-OPTIONAL (ORPHAN) DISPATCH
// Verifies `void on(Event&)` binds correctly when Event has NO registered context.
// ============================================================================

class EvOrphan {};
EXPECT_INHERITANCE(EvOrphan);

class HndOrphan {
public:
    using priority = slabflux::priority<1>;
    static inline int stateless_hits = 0;

    void on(EvOrphan&) {
        stateless_hits += 99;
    }
};
EXPECT_INHERITANCE(HndOrphan);

// ============================================================================
// EDGE CASE 2: GOD-OBJECT SLICING (MULTIPLE INHERITANCE RESOLUTION)
// Validates strict topological sorting and upcasting across disjoint bases.
// ============================================================================

struct GodContext { int essence = 0; };

class EvBaseX {
public:
    using priority = slabflux::priority<10>;
    virtual ~EvBaseX() = default;
};
EXPECT_INHERITANCE(EvBaseX);

class EvBaseY {
public:
    using priority = slabflux::priority<20>;
    virtual ~EvBaseY() = default;
};
EXPECT_INHERITANCE(EvBaseY);

class EvBaseZ {
public:
    using priority = slabflux::priority<30>;
    virtual ~EvBaseZ() = default;
};
EXPECT_INHERITANCE(EvBaseZ);

class EvGod : public EvBaseX, public EvBaseY, public EvBaseZ {
public:
    using priority = slabflux::priority<40>;
};
EXPECT_INHERITANCE(EvGod, EvBaseX, EvBaseY, EvBaseZ);

REGISTER_CONTEXT(EvBaseX, GodContext);
REGISTER_CONTEXT(EvBaseY, GodContext);
REGISTER_CONTEXT(EvBaseZ, GodContext);
REGISTER_CONTEXT(EvGod, GodContext);

class HndX {
public:
    using priority = slabflux::priority<10>;
    void on(EvBaseX&, GodContext& ctx) { ctx.essence += 1; }
};
EXPECT_INHERITANCE(HndX);

class HndY {
public:
    using priority = slabflux::priority<20>;
    void on(EvBaseY&, GodContext& ctx) { ctx.essence += 10; }
};
EXPECT_INHERITANCE(HndY);

class HndZ {
public:
    using priority = slabflux::priority<30>;
    void on(EvBaseZ&, GodContext& ctx) { ctx.essence += 100; }
};
EXPECT_INHERITANCE(HndZ);

class HndGod : public HndX, public HndY, public HndZ {
public:
    using priority = slabflux::priority<40>;
    void on(EvGod&, GodContext& ctx) { ctx.essence += 1000; }
};
EXPECT_INHERITANCE(HndGod, HndX, HndY, HndZ);

// ============================================================================
// EDGE CASE 3: VARIADIC CROSS-LANE HALTING
// Tests boolean short-circuit logic across disjoint handlers in a pipeline array.
// ============================================================================

struct VarHaltCtx { int counter = 0; };

class EvVarHalt {};
REGISTER_CONTEXT(EvVarHalt, VarHaltCtx);
EXPECT_INHERITANCE(EvVarHalt);

class HndVar1 {
public:
    using priority = slabflux::priority<5>;
    bool on(EvVarHalt&, VarHaltCtx& ctx) {
        ctx.counter += 5;
        return false;
    }
};
EXPECT_INHERITANCE(HndVar1);

class HndVar2 {
public:
    using priority = slabflux::priority<15>;
    bool on(EvVarHalt&, VarHaltCtx& ctx) {
        ctx.counter += 50;
        return true; // HALT TRIGGERED HERE
    }
};
EXPECT_INHERITANCE(HndVar2);

class HndVar3 {
public:
    using priority = slabflux::priority<25>;
    bool on(EvVarHalt&, VarHaltCtx& ctx) {
        ctx.counter += 500; // Unreachable
        return false;
    }
};
EXPECT_INHERITANCE(HndVar3);

// ============================================================================
// MAIN EXECUTION THREAD
// ============================================================================

TEST(PipelineEdgeCases, ContextFreeOrphan) {
    {
        HndOrphan::stateless_hits = 0;
        slabflux::core::pipeline<HndOrphan> dispatcher(HndOrphan{});
        EvOrphan eventInstance;

        dispatcher.dispatch(eventInstance);
        EXPECT_EQ(HndOrphan::stateless_hits, 99);
    }
}

TEST(PipelineEdgeCases, GodObjectSlicing) {
    {
        GodContext ctx;
        slabflux::core::pipeline<HndGod> dispatcher(HndGod{});
        EvGod godInstance;

        dispatcher.dispatch(ctx, godInstance);
        EXPECT_EQ(ctx.essence, 1111);
    }
}

TEST(PipelineEdgeCases, CrossLaneHalt) {
    {
        VarHaltCtx ctx;
        slabflux::core::pipeline<HndVar1, HndVar2, HndVar3> dispatcher(HndVar1{}, HndVar2{}, HndVar3{});
        EvVarHalt target_event;

        dispatcher.dispatch(ctx, target_event);
        EXPECT_EQ(ctx.counter, 55);
    }
}
