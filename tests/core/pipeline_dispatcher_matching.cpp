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
 * ============================================================================* @file test_dispatcher_matching.cpp
 * @brief Full Integration Test for Event Matching & Cartesian DAG Unrolling
 */

#include <iostream>
#include <string>

#include <gtest/gtest.h>
#include "slabflux/core/pipeline.hpp"
#include "slabflux/pipeline/context_vault.hpp"

// ============================================================================
// 1. CONTEXT AND EVENT HIERARCHY
// ============================================================================

struct MatchCtx {
    int base_hits = 0;
    int derived_hits = 0;
    int const_hits = 0;
    int unrelated_hits = 0;
};

// Events (Using built-in parents types to bypass meta_traits for testing)
class EvBase {
public:
    using parents = slabflux::typelist<>;
    virtual ~EvBase() = default;
};

class EvDerived : public EvBase {
public:
    using parents = slabflux::typelist<EvBase>;
};

class EvUnrelated {
public:
    using parents = slabflux::typelist<>;
};

// Context Regisztráció
REGISTER_CONTEXT(EvBase, MatchCtx);
REGISTER_CONTEXT(EvDerived, MatchCtx);
REGISTER_CONTEXT(EvUnrelated, MatchCtx);

// ============================================================================
// 2. HANDLER HIERARCHIES FOR EDGE CASES
// ============================================================================

// --- C1: Exact Match (Non-Const) ---
class HndExact {
public:
    using parents = slabflux::typelist<>;
    void on(EvBase&, MatchCtx& ctx) { ctx.base_hits++; }
};

// --- C2: Exact Match (Const) ---
class HndConst {
public:
    using parents = slabflux::typelist<>;
    void on(const EvBase&, MatchCtx& ctx) { ctx.const_hits++; }
};

// --- C3: Exact Match (Derived) ---
class HndDerived {
public:
    using parents = slabflux::typelist<>;
    void on(EvDerived&, MatchCtx& ctx) { ctx.derived_hits++; }
};

// --- C4: Handler Inheritance & Name Hiding ---
class HndHideRoot {
public:
    using parents = slabflux::typelist<>;
    void on(EvBase&, MatchCtx& ctx) { ctx.base_hits++; }
};

class HndHideLeaf : public HndHideRoot {
public:
    using parents = slabflux::typelist<HndHideRoot>;
    // According to standard C++, this HIDES the HndHideRoot::on(EvBase&) method!
    void on(EvUnrelated&, MatchCtx& ctx) { ctx.unrelated_hits++; }
};

// ============================================================================
// 3. MAIN TEST SUITE
// ============================================================================

TEST(PipelineDispatcherMatching, UpcastingSlicing) {
    {
        MatchCtx ctx;
        slabflux::core::pipeline<HndExact> p(HndExact{});
        EvDerived event;

        p.dispatch(ctx, event);
        EXPECT_EQ(ctx.base_hits, 1);
    }
}

TEST(PipelineDispatcherMatching, DowncastRejection) {
    {
        MatchCtx ctx;
        slabflux::core::pipeline<HndDerived> p(HndDerived{});
        EvBase event;

        p.dispatch(ctx, event);
        EXPECT_EQ(ctx.derived_hits, 0);
    }
}

TEST(PipelineDispatcherMatching, ConstCorrectness) {
    {
        MatchCtx ctx;
        slabflux::core::pipeline<HndConst> p(HndConst{});
        EvBase event;

        p.dispatch(ctx, event);
        EXPECT_EQ(ctx.const_hits, 1);
    }
}

TEST(PipelineDispatcherMatching, NameHidingBypass) {
    {
        MatchCtx ctx;
        slabflux::core::pipeline<HndHideLeaf> p(HndHideLeaf{});
        EvBase event;

        p.dispatch(ctx, event);
        EXPECT_EQ(ctx.base_hits, 1);
        EXPECT_EQ(ctx.unrelated_hits, 0);
    }
}

TEST(PipelineDispatcherMatching, LeafExplicitHit) {
    {
        MatchCtx ctx;
        slabflux::core::pipeline<HndHideLeaf> p(HndHideLeaf{});
        EvUnrelated event;

        p.dispatch(ctx, event);
        EXPECT_EQ(ctx.unrelated_hits, 1);
    }
}
