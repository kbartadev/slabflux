// ============================================================================
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
 * ============================================================================* @file tests_implicit_priority.cpp
 * @brief Validation of Implicit Topological Sorting and Priority Defaults
 */ 

#include <iostream>
#include <gtest/gtest.h>
#include "slabflux/core/pipeline.hpp"
#include "slabflux/pipeline/context_vault.hpp"

// ============================================================================
// IMPLICIT TEST 1: PURE TOPOLOGY (ZERO PRIORITIES)
// Validates that deterministic top-down unrolling requires NO priority tags
// if a structural inheritance chain is present.
// ============================================================================

struct PureCtx { int sequence = 0; };

class PureRoot { public: virtual ~PureRoot() = default; };
EXPECT_INHERITANCE(PureRoot);
class PureMid : public virtual PureRoot {};
EXPECT_INHERITANCE(PureMid, PureRoot);
class PureLeaf : public virtual PureMid {};
EXPECT_INHERITANCE(PureLeaf, PureMid);

REGISTER_CONTEXT(PureRoot, PureCtx);
REGISTER_CONTEXT(PureMid, PureCtx);
REGISTER_CONTEXT(PureLeaf, PureCtx);

// Notice: ZERO `using priority = ...` declarations.
// They all default to Infinity, but topology breaks the ties automatically!

class HndPureRoot {
public:
    void on(PureRoot&, PureCtx& ctx) { ctx.sequence = (ctx.sequence * 10) + 1; }
};
EXPECT_INHERITANCE(HndPureRoot);

class HndPureMid : public virtual HndPureRoot {
public:
    void on(PureMid&, PureCtx& ctx) { ctx.sequence = (ctx.sequence * 10) + 2; }
};
EXPECT_INHERITANCE(HndPureMid, HndPureRoot);

class HndPureLeaf : public virtual HndPureMid {
public:
    void on(PureLeaf&, PureCtx& ctx) { ctx.sequence = (ctx.sequence * 10) + 3; }
};
EXPECT_INHERITANCE(HndPureLeaf, HndPureMid);


// ============================================================================
// IMPLICIT TEST 2: THE "INFINITY" FALLBACK MATRIX
// Validates that absent priorities mathematically evaluate to the maximum
// integer limit, guaranteeing they execute AFTER all explicitly prioritized peers.
// ============================================================================

struct LatCtx { int sequence = 0; };

class LatEv {};
REGISTER_CONTEXT(LatEv, LatCtx);
EXPECT_INHERITANCE(LatEv);

class HndLatA {
public:
    // NO PRIORITY DEFINED. Framework mathematically evaluates as 99.
    void on(LatEv&, LatCtx& ctx) { ctx.sequence = (ctx.sequence * 10) + 1; }
};
EXPECT_INHERITANCE(HndLatA);

class HndLatB {
public:
    // Explicit priority 50. Must execute BEFORE HndLatC and HndLatA.
    using priority = slabflux::priority<50>;
    void on(LatEv&, LatCtx& ctx) { ctx.sequence = (ctx.sequence * 10) + 2; }
};
EXPECT_INHERITANCE(HndLatB);

class HndLatC {
public:
    // Explicit priority 150. Must execute AFTER HndLatB but BEFORE HndLatA.
    using priority = slabflux::priority<150>;
    void on(LatEv&, LatCtx& ctx) { ctx.sequence = (ctx.sequence * 10) + 3; }
};
EXPECT_INHERITANCE(HndLatC);

// ============================================================================
// MAIN EXECUTION THREAD
// ============================================================================

TEST(PipelineImplicitPriority, PureImplicitTopologicalExecution) {
    {
        PureCtx ctx;
        slabflux::core::pipeline<HndPureLeaf> dispatcher(HndPureLeaf{});
        PureLeaf eventInstance;

        dispatcher.dispatch(ctx, eventInstance);
        EXPECT_EQ(ctx.sequence, 123);
    }
}

TEST(PipelineImplicitPriority, MathematicalInfinityFallbackResolution) {
    {
        LatCtx ctx;
        slabflux::core::pipeline<HndLatC, HndLatA, HndLatB> dispatcher(HndLatC{}, HndLatA{}, HndLatB{});
        LatEv eventInstance;

        dispatcher.dispatch(ctx, eventInstance);
        EXPECT_EQ(ctx.sequence, 231);
    }
}
