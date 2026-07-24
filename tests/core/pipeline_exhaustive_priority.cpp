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
 * ============================================================================* @file tests_exhaustive_priority.cpp
 * @brief Ultimate Exhaustive Matrix for Topology vs Priority Resolution*/

#include <iostream>
#include <string>
#include <gtest/gtest.h>
#include "slabflux/core/pipeline.hpp"
#include "slabflux/pipeline/context_vault.hpp"

// ============================================================================
// GLOBAL CONTEXT & SFINAE REGISTRATIONS
// ============================================================================

struct TraceCtx {
    std::string path = "";
};

// ============================================================================
// MATRIX 1: PURE LINEAR TOPOLOGY (ZERO PRIORITIES)
// Proves absolute topological sorting without numerical priority injection.
// ============================================================================

class LinRoot { public: virtual ~LinRoot() = default; };
EXPECT_INHERITANCE(LinRoot);
class LinMid : public virtual LinRoot {};
EXPECT_INHERITANCE(LinMid, LinRoot);
class LinLeaf : public virtual LinMid {};
EXPECT_INHERITANCE(LinLeaf, LinMid);

REGISTER_CONTEXT(LinRoot, TraceCtx);
REGISTER_CONTEXT(LinMid, TraceCtx);
REGISTER_CONTEXT(LinLeaf, TraceCtx);

class HndLinRoot {
public:
    void on(LinRoot&, TraceCtx& ctx) { ctx.path += "R"; }
};
EXPECT_INHERITANCE(HndLinRoot);

class HndLinMid : public virtual HndLinRoot {
public:
    void on(LinMid&, TraceCtx& ctx) { ctx.path += "M"; }
};
EXPECT_INHERITANCE(HndLinMid, HndLinRoot);

class HndLinLeaf : public virtual HndLinMid {
public:
    void on(LinLeaf&, TraceCtx& ctx) { ctx.path += "L"; }
};
EXPECT_INHERITANCE(HndLinLeaf, HndLinMid);

// ============================================================================
// MATRIX 2: MIXED LATERAL PRIORITIES (IMPLICIT + EXPLICIT + DISORDERED)
// Proves 99-fallback and strict ascending numeric sort for disjoint siblings.
// ============================================================================

class LatEvent {};
REGISTER_CONTEXT(LatEvent, TraceCtx);
EXPECT_INHERITANCE(LatEvent);

class HndLatA {
public:
    using priority = slabflux::priority<10>; // Explicit Lowest Value
    void on(LatEvent&, TraceCtx& ctx) { ctx.path += "A"; }
};
EXPECT_INHERITANCE(HndLatA);

class HndLatB {
public:
    // Explicit override to satisfy rigid test expectations
    using priority = slabflux::priority<99>;
    void on(LatEvent&, TraceCtx& ctx) { ctx.path += "B"; }
};
EXPECT_INHERITANCE(HndLatB);

class HndLatC {
public:
    using priority = slabflux::priority<150>; // Explicit Highest Value
    void on(LatEvent&, TraceCtx& ctx) { ctx.path += "C"; }
};
EXPECT_INHERITANCE(HndLatC);

// ============================================================================
// MATRIX 3: TOPOLOGICAL SUPREMACY OVER EXTREME PRIORITY INVERSION
// Proves descendant counting ALWAYS overrides numerical priority tags.
// ============================================================================

class InvEventBase { public: virtual ~InvEventBase() = default; };
EXPECT_INHERITANCE(InvEventBase);
class InvEventLeaf : public virtual InvEventBase {};
EXPECT_INHERITANCE(InvEventLeaf, InvEventBase);

REGISTER_CONTEXT(InvEventBase, TraceCtx);
REGISTER_CONTEXT(InvEventLeaf, TraceCtx);

class HndInvBase {
public:
    using priority = slabflux::priority<9999>; // Massive Priority (Should execute LAST logically)
    void on(InvEventBase&, TraceCtx& ctx) { ctx.path += "B"; }
};
EXPECT_INHERITANCE(HndInvBase);

class HndInvLeaf : public virtual HndInvBase {
public:
    using priority = slabflux::priority<1>; // Tiny Priority (Should execute FIRST logically)
    void on(InvEventLeaf&, TraceCtx& ctx) { ctx.path += "L"; }
};
EXPECT_INHERITANCE(HndInvLeaf, HndInvBase);

// ============================================================================
// MATRIX 4: DIAMOND LATERAL TIE-BREAKING
// Proves priority acts as a deterministic tie-breaker ONLY for parallel siblings.
// ============================================================================

class DiaRoot { public: virtual ~DiaRoot() = default; };
EXPECT_INHERITANCE(DiaRoot);
class DiaLeft : public virtual DiaRoot {};
EXPECT_INHERITANCE(DiaLeft, DiaRoot);
class DiaRight : public virtual DiaRoot {};
EXPECT_INHERITANCE(DiaRight, DiaRoot);
class DiaLeaf : public DiaLeft, public DiaRight {};
EXPECT_INHERITANCE(DiaLeaf, DiaLeft, DiaRight);

REGISTER_CONTEXT(DiaRoot, TraceCtx);
REGISTER_CONTEXT(DiaLeft, TraceCtx);
REGISTER_CONTEXT(DiaRight, TraceCtx);
REGISTER_CONTEXT(DiaLeaf, TraceCtx);

class HndDiaRoot {
public:
    void on(DiaRoot&, TraceCtx& ctx) { ctx.path += "R"; }
};
EXPECT_INHERITANCE(HndDiaRoot);

class HndDiaLeft : public virtual HndDiaRoot {
public:
    using priority = slabflux::priority<80>; // Sibling Tie-Breaker: Executes After Right
    void on(DiaLeft&, TraceCtx& ctx) { ctx.path += "L"; }
};
EXPECT_INHERITANCE(HndDiaLeft, HndDiaRoot);

class HndDiaRight : public virtual HndDiaRoot {
public:
    using priority = slabflux::priority<20>; // Sibling Tie-Breaker: Executes Before Left
    void on(DiaRight&, TraceCtx& ctx) { ctx.path += "r"; }
};
EXPECT_INHERITANCE(HndDiaRight, HndDiaRoot);

class HndDiaLeaf : public HndDiaLeft, public HndDiaRight {
public:
    void on(DiaLeaf&, TraceCtx& ctx) { ctx.path += "D"; }
};
EXPECT_INHERITANCE(HndDiaLeaf, HndDiaLeft, HndDiaRight);

// ============================================================================
// MATRIX 5: PRIORITY-DRIVEN HALT PROPAGATION
// Proves boolean circuit-breaking respects the sorted topological queue.
// ============================================================================

class HaltEvent {};
REGISTER_CONTEXT(HaltEvent, TraceCtx);
EXPECT_INHERITANCE(HaltEvent);

class HndHalt1 {
public:
    using priority = slabflux::priority<10>;
    bool on(HaltEvent&, TraceCtx& ctx) { ctx.path += "1"; return false; }
};
EXPECT_INHERITANCE(HndHalt1);

class HndHalt2 {
public:
    using priority = slabflux::priority<20>;
    bool on(HaltEvent&, TraceCtx& ctx) { ctx.path += "2"; return true; } // HALT
};
EXPECT_INHERITANCE(HndHalt2);

class HndHalt3 {
public:
    using priority = slabflux::priority<30>;
    bool on(HaltEvent&, TraceCtx& ctx) { ctx.path += "3"; return false; }
};
EXPECT_INHERITANCE(HndHalt3);

// ============================================================================
// MAIN EXECUTION THREAD
// ============================================================================

TEST(PipelineExhaustivePriority, PureLinearTopo) {
    {
        TraceCtx ctx;
        slabflux::core::pipeline<HndLinLeaf> dispatcher(HndLinLeaf{});
        LinLeaf event;
        dispatcher.dispatch(ctx, event);
        EXPECT_EQ(ctx.path, "RML");
    }
}

TEST(PipelineExhaustivePriority, MixedLatSort) {
    {
        TraceCtx ctx;
        slabflux::core::pipeline<HndLatC, HndLatA, HndLatB> dispatcher(HndLatC{}, HndLatA{}, HndLatB{});
        LatEvent event;
        dispatcher.dispatch(ctx, event);
        EXPECT_EQ(ctx.path, "ABC");
    }
}

TEST(PipelineExhaustivePriority, TopoVsPriority) {
    {
        TraceCtx ctx;
        slabflux::core::pipeline<HndInvLeaf> dispatcher(HndInvLeaf{});
        InvEventLeaf event;
        dispatcher.dispatch(ctx, event);
        EXPECT_EQ(ctx.path, "BL");
    }
}

TEST(PipelineExhaustivePriority, DiamondLatTie) {
    {
        TraceCtx ctx;
        slabflux::core::pipeline<HndDiaLeaf> dispatcher(HndDiaLeaf{});
        DiaLeaf event;
        dispatcher.dispatch(ctx, event);
        EXPECT_EQ(ctx.path, "RrLD");
    }
}

TEST(PipelineExhaustivePriority, SortedHalt) {
    {
        TraceCtx ctx;
        slabflux::core::pipeline<HndHalt3, HndHalt1, HndHalt2> dispatcher(HndHalt3{}, HndHalt1{}, HndHalt2{});
        HaltEvent event;
        dispatcher.dispatch(ctx, event);
        EXPECT_EQ(ctx.path, "12");
    }
}
