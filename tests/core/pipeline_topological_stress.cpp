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
 * ============================================================================* @file tests_topological_stress.cpp
 * @brief Natively defined topological inheritance (Zero MOC Dependency)
 */

#include <iostream>
#include <gtest/gtest.h>
#include "slabflux/core/pipeline.hpp"
#include "slabflux/pipeline/context_vault.hpp"

// ============================================================================
// TOPOLOGY STRESS 1: PURE DIAMOND HIERARCHY TRACE
// Validates symmetric lateral traversal order merging without redundant calls.
// ============================================================================

struct TopoCtx {
    int sequence = 0;
};

// --- EVENTS (Now with deterministic priority bounds to prevent lateral ambiguity) ---
class TopoRoot {
public:
    using parents = slabflux::typelist<>;
    using priority = slabflux::priority<10>; // Baseline
    virtual ~TopoRoot() = default;
};
EXPECT_INHERITANCE(TopoRoot);

class TopoLeft : public virtual TopoRoot {
public:
    using parents = slabflux::typelist<TopoRoot>;
    using priority = slabflux::priority<20>; // Left executes before Right
};
EXPECT_INHERITANCE(TopoLeft, TopoRoot);

class TopoRight : public virtual TopoRoot {
public:
    using parents = slabflux::typelist<TopoRoot>;
    using priority = slabflux::priority<30>;
};
EXPECT_INHERITANCE(TopoRight, TopoRoot);

class TopoDiamond : public TopoLeft, public TopoRight {
public:
    using parents = slabflux::typelist<TopoLeft, TopoRight>;
    using priority = slabflux::priority<40>;
};
EXPECT_INHERITANCE(TopoDiamond, TopoLeft, TopoRight);

REGISTER_CONTEXT(TopoRoot, TopoCtx);
REGISTER_CONTEXT(TopoLeft, TopoCtx);
REGISTER_CONTEXT(TopoRight, TopoCtx);
REGISTER_CONTEXT(TopoDiamond, TopoCtx);

// --- HANDLERS ---
class HndTopoRoot {
public:
    using parents = slabflux::typelist<>;
    using priority = slabflux::priority<10>;
    void on(TopoRoot&, TopoCtx& ctx) {
        ctx.sequence = (ctx.sequence * 10) + 1;
    }
};
EXPECT_INHERITANCE(HndTopoRoot);

class HndTopoLeft : public virtual HndTopoRoot {
public:
    using parents = slabflux::typelist<HndTopoRoot>;
    using priority = slabflux::priority<20>;
    void on(TopoLeft&, TopoCtx& ctx) {
        ctx.sequence = (ctx.sequence * 10) + 2;
    }
};
EXPECT_INHERITANCE(HndTopoLeft, HndTopoRoot);

class HndTopoRight : public virtual HndTopoRoot {
public:
    using parents = slabflux::typelist<HndTopoRoot>;
    using priority = slabflux::priority<30>;
    void on(TopoRight&, TopoCtx& ctx) {
        ctx.sequence = (ctx.sequence * 10) + 3;
    }
};
EXPECT_INHERITANCE(HndTopoRight, HndTopoRoot);

class HndTopoDiamond : public HndTopoLeft, public HndTopoRight {
public:
    using parents = slabflux::typelist<HndTopoLeft, HndTopoRight>;
    using priority = slabflux::priority<40>;
    void on(TopoDiamond&, TopoCtx& ctx) {
        ctx.sequence = (ctx.sequence * 10) + 4;
    }
};
EXPECT_INHERITANCE(HndTopoDiamond, HndTopoLeft, HndTopoRight);

// ============================================================================
// TOPOLOGY STRESS 2: INVERSE PRIORITY OVERRIDE
// Validates that explicit descendant counting topologically sorts Base BEFORE
// Leaf, absolutely ignoring synthetic priority numbers that attempt inversion.
// ============================================================================

struct InvCtx {
    int exec_val = 0;
};

// --- EVENTS ---
class InvBase {
public:
    using parents = slabflux::typelist<>;
    using priority = slabflux::priority<10>;
    virtual ~InvBase() = default;
};
EXPECT_INHERITANCE(InvBase);

class InvLeaf : public InvBase {
public:
    using parents = slabflux::typelist<InvBase>;
    using priority = slabflux::priority<20>;
};
EXPECT_INHERITANCE(InvLeaf, InvBase);

class InvLeafParallel : public InvBase {
public:
    using parents = slabflux::typelist<InvBase>;
    using priority = slabflux::priority<30>;
};
EXPECT_INHERITANCE(InvLeafParallel, InvBase);

REGISTER_CONTEXT(InvBase, InvCtx);
REGISTER_CONTEXT(InvLeaf, InvCtx);
REGISTER_CONTEXT(InvLeafParallel, InvCtx);

// --- HANDLERS ---
class HndInvBase {
public:
    using parents = slabflux::typelist<>;
    using priority = slabflux::priority<99>; // Synthetic lowest priority
    void on(InvBase&, InvCtx& ctx) {
        ctx.exec_val += 1;
    }
};
EXPECT_INHERITANCE(HndInvBase);

class HndInvLeaf : public HndInvBase {
public:
    using parents = slabflux::typelist<HndInvBase>;
    using priority = slabflux::priority<50>; // Synthetic highest priority
    void on(InvLeaf&, InvCtx& ctx) {
        ctx.exec_val += 10;
    }
};
EXPECT_INHERITANCE(HndInvLeaf, HndInvBase);

class HndInvLeafParallel : public HndInvBase {
public:
    using parents = slabflux::typelist<HndInvBase>;
    using priority = slabflux::priority<10>;
    void on(InvLeafParallel&, InvCtx& ctx) {
        ctx.exec_val += 100;
    }
};
EXPECT_INHERITANCE(HndInvLeafParallel, HndInvBase);

// ============================================================================
// MAIN EXECUTION THREAD
// ============================================================================

TEST(PipelineTopologicalStress, DiamondSortLexicography) {
    {
        TopoCtx ctx;
        slabflux::core::pipeline<HndTopoDiamond> dispatcher(HndTopoDiamond{});
        TopoDiamond eventInstance;

        dispatcher.dispatch(ctx, eventInstance);
        EXPECT_EQ(ctx.sequence, 1234);
    }
}

TEST(PipelineTopologicalStress, InverseTopologySortLaw) {
    {
        InvCtx ctx;
        slabflux::core::pipeline<HndInvLeaf, HndInvLeafParallel> dispatcher(HndInvLeaf{}, HndInvLeafParallel{});
        InvLeaf eventInstance;

        dispatcher.dispatch(ctx, eventInstance);
        EXPECT_EQ(ctx.exec_val, 11);
    }
}
