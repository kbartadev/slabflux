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
 * ============================================================================* @file tests_sporadic_priority.cpp
 * @brief Complex Topology with Sporadic/Sparse Priorities
 */

#include <iostream>
#include <string>
#include <gtest/gtest.h>
#include "slabflux/core/pipeline.hpp"
#include "slabflux/pipeline/context_vault.hpp"

// ============================================================================
// GLOBAL CONTEXT
// ============================================================================

struct SporadicCtx {
    std::string path = "";
};

// ============================================================================
// TOPOLOGY DEFINITION: THE "SPARSE SPIDER" DAG
//
//          Root           (Level 0: 8 descendants)
//         /  |  \
//       A    B    C       (Level 1: 3 descendants each)
//       |    |    |
//       D    E    F       (Level 2: 2 descendants each)
//        \   |   /
//          Leaf           (Level 3: 1 descendant)
//            |
//          Tail           (Level 4: 0 descendants)
//
// PRIORITIES INJECTED:
// Level 1: B=10, C=20, A=(Missing/Implicit) -> Expected: B, C, A
// Level 2: E=5, D=(Missing), F=(Missing)    -> Expected: E, D, F
// ============================================================================

class SpRoot { public: virtual ~SpRoot() = default; };
EXPECT_INHERITANCE(SpRoot);

class SpA : public virtual SpRoot {};
EXPECT_INHERITANCE(SpA, SpRoot);
class SpB : public virtual SpRoot {};
EXPECT_INHERITANCE(SpB, SpRoot);
class SpC : public virtual SpRoot {};
EXPECT_INHERITANCE(SpC, SpRoot);

class SpD : public virtual SpA {};
EXPECT_INHERITANCE(SpD, SpA);
class SpE : public virtual SpB {};
EXPECT_INHERITANCE(SpE, SpB);
class SpF : public virtual SpC {};
EXPECT_INHERITANCE(SpF, SpC);

class SpLeaf : public SpD, public SpE, public SpF {};
EXPECT_INHERITANCE(SpLeaf, SpD, SpE, SpF);
class SpTail : public SpLeaf {};
EXPECT_INHERITANCE(SpTail, SpLeaf);

REGISTER_CONTEXT(SpRoot, SporadicCtx);
REGISTER_CONTEXT(SpA, SporadicCtx);
REGISTER_CONTEXT(SpB, SporadicCtx);
REGISTER_CONTEXT(SpC, SporadicCtx);
REGISTER_CONTEXT(SpD, SporadicCtx);
REGISTER_CONTEXT(SpE, SporadicCtx);
REGISTER_CONTEXT(SpF, SporadicCtx);
REGISTER_CONTEXT(SpLeaf, SporadicCtx);
REGISTER_CONTEXT(SpTail, SporadicCtx);

// --- HANDLERS (SPORADIC PRIORITIES) ---

class HndSpRoot {
public:
    void on(SpRoot&, SporadicCtx& ctx) { ctx.path += "R_"; }
};
EXPECT_INHERITANCE(HndSpRoot);

class HndSpA : public virtual HndSpRoot {
public:
    // NO PRIORITY
    void on(SpA&, SporadicCtx& ctx) { ctx.path += "A_"; }
};
EXPECT_INHERITANCE(HndSpA, HndSpRoot);

class HndSpB : public virtual HndSpRoot {
public:
    using priority = slabflux::priority<10>; // EXPLICIT HIGH
    void on(SpB&, SporadicCtx& ctx) { ctx.path += "B_"; }
};
EXPECT_INHERITANCE(HndSpB, HndSpRoot);

class HndSpC : public virtual HndSpRoot {
public:
    using priority = slabflux::priority<20>; // EXPLICIT MID
    void on(SpC&, SporadicCtx& ctx) { ctx.path += "C_"; }
};
EXPECT_INHERITANCE(HndSpC, HndSpRoot);

class HndSpD : public virtual HndSpA {
public:
    // NO PRIORITY
    void on(SpD&, SporadicCtx& ctx) { ctx.path += "D_"; }
};
EXPECT_INHERITANCE(HndSpD, HndSpA);

class HndSpE : public virtual HndSpB {
public:
    using priority = slabflux::priority<5>; // EXPLICIT SUPER HIGH
    void on(SpE&, SporadicCtx& ctx) { ctx.path += "E_"; }
};
EXPECT_INHERITANCE(HndSpE, HndSpB);

class HndSpF : public virtual HndSpC {
public:
    // NO PRIORITY
    void on(SpF&, SporadicCtx& ctx) { ctx.path += "F_"; }
};
EXPECT_INHERITANCE(HndSpF, HndSpC);

class HndSpLeaf : public HndSpD, public HndSpE, public HndSpF {
public:
    // NO PRIORITY
    void on(SpLeaf&, SporadicCtx& ctx) { ctx.path += "Leaf_"; }
};
EXPECT_INHERITANCE(HndSpLeaf, HndSpD, HndSpE, HndSpF);

class HndSpTail : public HndSpLeaf {
public:
    // NO PRIORITY
    void on(SpTail&, SporadicCtx& ctx) { ctx.path += "Tail"; }
};
EXPECT_INHERITANCE(HndSpTail, HndSpLeaf);

// ============================================================================
// MAIN EXECUTION THREAD
// ============================================================================

TEST(PipelineSporadicPriority, SparseSpider) {
    SporadicCtx ctx;

    slabflux::core::pipeline<HndSpTail> dispatcher(HndSpTail{});
    SpTail event;
    dispatcher.dispatch(ctx, event);

    EXPECT_EQ(ctx.path, "R_A_B_C_D_E_F_Leaf_Tail");
}
