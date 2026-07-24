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
 * ============================================================================*/

#include <gtest/gtest.h>
#include <string>
#include "slabflux/core/pipeline.hpp"
#include "slabflux/pipeline/context_vault.hpp"

// ============================================================================
// BASELINE INTEGRATION 1: DISPATCHER MATCHING
// ============================================================================
namespace dispatcher_matching_test {
    struct MatchCtx {
        int base_hits = 0;
        int derived_hits = 0;
        int const_hits = 0;
        int unrelated_hits = 0;
    };

    class EvBase {
    public:
        using parents = slabflux::typelist<>;
        virtual ~EvBase() = default;
    };
    EXPECT_INHERITANCE(EvBase);

    class EvDerived : public EvBase {
    public:
        using parents = slabflux::typelist<EvBase>;
    };
    EXPECT_INHERITANCE(EvDerived, EvBase);

    class EvUnrelated {
    public:
        using parents = slabflux::typelist<>;
    };
    EXPECT_INHERITANCE(EvUnrelated);
}
REGISTER_CONTEXT(dispatcher_matching_test::EvBase, dispatcher_matching_test::MatchCtx)
REGISTER_CONTEXT(dispatcher_matching_test::EvDerived, dispatcher_matching_test::MatchCtx)
REGISTER_CONTEXT(dispatcher_matching_test::EvUnrelated, dispatcher_matching_test::MatchCtx)

namespace dispatcher_matching_test {
    class HndExact {
    public:
        using parents = slabflux::typelist<>;
        void on(EvBase&, MatchCtx& ctx) { ctx.base_hits++; }
    };
    EXPECT_INHERITANCE(HndExact);

    class HndConst {
    public:
        using parents = slabflux::typelist<>;
        void on(const EvBase&, MatchCtx& ctx) { ctx.const_hits++; }
    };
    EXPECT_INHERITANCE(HndConst);

    class HndDerived {
    public:
        using parents = slabflux::typelist<>;
        void on(EvDerived&, MatchCtx& ctx) { ctx.derived_hits++; }
    };
    EXPECT_INHERITANCE(HndDerived);

    class HndHideRoot {
    public:
        using parents = slabflux::typelist<>;
        void on(EvBase&, MatchCtx& ctx) { ctx.base_hits++; }
    };
    EXPECT_INHERITANCE(HndHideRoot);

    class HndHideLeaf : public HndHideRoot {
    public:
        using parents = slabflux::typelist<HndHideRoot>;
        void on(EvUnrelated&, MatchCtx& ctx) { ctx.unrelated_hits++; }
    };
    EXPECT_INHERITANCE(HndHideLeaf, HndHideRoot);
}

TEST(BaselineDispatcherMatching, UpcastingSlicing) {
    dispatcher_matching_test::MatchCtx ctx;
    slabflux::core::pipeline<dispatcher_matching_test::HndExact> p(dispatcher_matching_test::HndExact{});
    dispatcher_matching_test::EvDerived event;
    p.dispatch(ctx, event);
    EXPECT_EQ(ctx.base_hits, 1);
}

TEST(BaselineDispatcherMatching, DowncastRejection) {
    dispatcher_matching_test::MatchCtx ctx;
    slabflux::core::pipeline<dispatcher_matching_test::HndDerived> p(dispatcher_matching_test::HndDerived{});
    dispatcher_matching_test::EvBase event;
    p.dispatch(ctx, event);
    EXPECT_EQ(ctx.derived_hits, 0);
}

TEST(BaselineDispatcherMatching, ConstCorrectness) {
    dispatcher_matching_test::MatchCtx ctx;
    slabflux::core::pipeline<dispatcher_matching_test::HndConst> p(dispatcher_matching_test::HndConst{});
    dispatcher_matching_test::EvBase event;
    p.dispatch(ctx, event);
    EXPECT_EQ(ctx.const_hits, 1);
}

TEST(BaselineDispatcherMatching, NameHidingBypass) {
    dispatcher_matching_test::MatchCtx ctx;
    slabflux::core::pipeline<dispatcher_matching_test::HndHideLeaf> p(dispatcher_matching_test::HndHideLeaf{});
    dispatcher_matching_test::EvBase event;
    p.dispatch(ctx, event);
    EXPECT_EQ(ctx.base_hits, 1);
    EXPECT_EQ(ctx.unrelated_hits, 0);
}

TEST(BaselineDispatcherMatching, LeafExplicitHit) {
    dispatcher_matching_test::MatchCtx ctx;
    slabflux::core::pipeline<dispatcher_matching_test::HndHideLeaf> p(dispatcher_matching_test::HndHideLeaf{});
    dispatcher_matching_test::EvUnrelated event;
    p.dispatch(ctx, event);
    EXPECT_EQ(ctx.unrelated_hits, 1);
}

// ============================================================================
// BASELINE INTEGRATION 2: CONTEXT SIDE EFFECTS
// ============================================================================
namespace context_side_effects_test {
    struct SideEffectCtx {
        int expected_hits = 0;
        int anomaly_hits = 0;
    };
    class EvCtxPresent {};
    EXPECT_INHERITANCE(EvCtxPresent);
    class EvCtxIgnored {};
    EXPECT_INHERITANCE(EvCtxIgnored);
    class EvCtxAbsent {};
    EXPECT_INHERITANCE(EvCtxAbsent);
}
REGISTER_CONTEXT(context_side_effects_test::EvCtxPresent, context_side_effects_test::SideEffectCtx)
REGISTER_CONTEXT(context_side_effects_test::EvCtxIgnored, context_side_effects_test::SideEffectCtx)

namespace context_side_effects_test {
    class HndCtxPresent {
    public:
        void on(EvCtxPresent&, SideEffectCtx& ctx) { ctx.expected_hits += 10; }
    };
    EXPECT_INHERITANCE(HndCtxPresent);

    class HndCtxIgnored {
    public:
        static inline int static_hits = 0;
        void on(EvCtxIgnored&) { static_hits += 20; }
    };
    EXPECT_INHERITANCE(HndCtxIgnored);

    class HndCtxAbsent {
    public:
        using priority = slabflux::priority<10>;
        static inline int static_hits = 0;
        void on(EvCtxAbsent&) { static_hits += 30; }
    };
    EXPECT_INHERITANCE(HndCtxAbsent);

    class HndGhost {
    public:
        using priority = slabflux::priority<20>;
        void on(EvCtxAbsent&, SideEffectCtx& ctx) { ctx.anomaly_hits += 999; }
    };
    EXPECT_INHERITANCE(HndGhost);
}

TEST(BaselineContextSideEffects, SideEffectIsolation) {
    context_side_effects_test::SideEffectCtx ctx1;
    slabflux::core::pipeline<context_side_effects_test::HndCtxPresent> p1(context_side_effects_test::HndCtxPresent{});
    context_side_effects_test::EvCtxPresent ev1;
    p1.dispatch(ctx1, ev1);
    EXPECT_EQ(ctx1.expected_hits, 10);

    context_side_effects_test::SideEffectCtx ctx2;
    context_side_effects_test::HndCtxIgnored::static_hits = 0;
    slabflux::core::pipeline<context_side_effects_test::HndCtxIgnored> p2(context_side_effects_test::HndCtxIgnored{});
    context_side_effects_test::EvCtxIgnored ev2;
    p2.dispatch(ctx2, ev2);
    EXPECT_EQ(context_side_effects_test::HndCtxIgnored::static_hits, 20);
    EXPECT_EQ(ctx2.expected_hits, 0);
    EXPECT_EQ(ctx2.anomaly_hits, 0);

    context_side_effects_test::HndCtxAbsent::static_hits = 0;
    slabflux::core::pipeline<context_side_effects_test::HndCtxAbsent, context_side_effects_test::HndGhost> p3(context_side_effects_test::HndCtxAbsent{}, context_side_effects_test::HndGhost{});
    context_side_effects_test::EvCtxAbsent ev3;
    p3.dispatch(ev3);
    EXPECT_EQ(context_side_effects_test::HndCtxAbsent::static_hits, 30); // Ghost safely bypassed without context
}

// ============================================================================
// BASELINE INTEGRATION 3: EDGE CASES & GOD-OBJECT SLICING
// ============================================================================
namespace edge_cases_test {
    struct GodContext { int essence = 0; };
    class EvBaseX { public: using priority = slabflux::priority<10>; virtual ~EvBaseX() = default; };
    EXPECT_INHERITANCE(EvBaseX);
    class EvBaseY { public: using priority = slabflux::priority<20>; virtual ~EvBaseY() = default; };
    EXPECT_INHERITANCE(EvBaseY);
    class EvBaseZ { public: using priority = slabflux::priority<30>; virtual ~EvBaseZ() = default; };
    EXPECT_INHERITANCE(EvBaseZ);
    class EvGod : public EvBaseX, public EvBaseY, public EvBaseZ {
    public:
        using parents = slabflux::typelist<EvBaseX, EvBaseY, EvBaseZ>;
        using priority = slabflux::priority<40>;
    };
    EXPECT_INHERITANCE(EvGod, EvBaseX, EvBaseY, EvBaseZ);
}
REGISTER_CONTEXT(edge_cases_test::EvBaseX, edge_cases_test::GodContext)
REGISTER_CONTEXT(edge_cases_test::EvBaseY, edge_cases_test::GodContext)
REGISTER_CONTEXT(edge_cases_test::EvBaseZ, edge_cases_test::GodContext)
REGISTER_CONTEXT(edge_cases_test::EvGod, edge_cases_test::GodContext)

namespace edge_cases_test {
    class HndX { public: using priority = slabflux::priority<10>; void on(EvBaseX&, GodContext& ctx) { ctx.essence += 1; } };
    EXPECT_INHERITANCE(HndX);
    class HndY { public: using priority = slabflux::priority<20>; void on(EvBaseY&, GodContext& ctx) { ctx.essence += 10; } };
    EXPECT_INHERITANCE(HndY);
    class HndZ { public: using priority = slabflux::priority<30>; void on(EvBaseZ&, GodContext& ctx) { ctx.essence += 100; } };
    EXPECT_INHERITANCE(HndZ);
    class HndGod : public HndX, public HndY, public HndZ {
    public:
        using parents = slabflux::typelist<HndX, HndY, HndZ>;
        using priority = slabflux::priority<40>;
        void on(EvGod&, GodContext& ctx) { ctx.essence += 1000; }
    };
    EXPECT_INHERITANCE(HndGod, HndX, HndY, HndZ);

    struct VarHaltCtx { int counter = 0; };
    class EvVarHalt {};
    EXPECT_INHERITANCE(EvVarHalt);
}
REGISTER_CONTEXT(edge_cases_test::EvVarHalt, edge_cases_test::VarHaltCtx)

namespace edge_cases_test {
    class HndVar1 { public: using priority = slabflux::priority<5>; bool on(EvVarHalt&, VarHaltCtx& ctx) { ctx.counter += 5; return false; } };
    EXPECT_INHERITANCE(HndVar1);
    class HndVar2 { public: using priority = slabflux::priority<15>; bool on(EvVarHalt&, VarHaltCtx& ctx) { ctx.counter += 50; return true; } };
    EXPECT_INHERITANCE(HndVar2);
    class HndVar3 { public: using priority = slabflux::priority<25>; bool on(EvVarHalt&, VarHaltCtx& ctx) { ctx.counter += 500; return false; } };
    EXPECT_INHERITANCE(HndVar3);
}

TEST(BaselineEdgeCases, GodObjectSlicing) {
    edge_cases_test::GodContext ctx;
    slabflux::core::pipeline<edge_cases_test::HndGod> p(edge_cases_test::HndGod{});
    edge_cases_test::EvGod godInstance;
    p.dispatch(ctx, godInstance);
    EXPECT_EQ(ctx.essence, 1111);
}

TEST(BaselineEdgeCases, CrossLaneHalt) {
    edge_cases_test::VarHaltCtx ctx;
    slabflux::core::pipeline<edge_cases_test::HndVar1, edge_cases_test::HndVar2, edge_cases_test::HndVar3> p(edge_cases_test::HndVar1{}, edge_cases_test::HndVar2{}, edge_cases_test::HndVar3{});
    edge_cases_test::EvVarHalt target_event;
    p.dispatch(ctx, target_event);
    EXPECT_EQ(ctx.counter, 55);
}

// ============================================================================
// BASELINE INTEGRATION 4: IMPLICIT PRIORITY & FALLBACK
// ============================================================================

// Native infinity-fallback target (must be global for exact alignment with pipeline.hpp check)
struct LatCtx { int sequence = 0; };
class LatEv {};
REGISTER_CONTEXT(LatEv, LatCtx)
EXPECT_INHERITANCE(LatEv);

class HndLatA {
public:
    void on(LatEv&, LatCtx& ctx) { ctx.sequence = (ctx.sequence * 10) + 1; }
};
EXPECT_INHERITANCE(HndLatA);
class HndLatB {
public:
    using priority = slabflux::priority<50>;
    void on(LatEv&, LatCtx& ctx) { ctx.sequence = (ctx.sequence * 10) + 2; }
};
EXPECT_INHERITANCE(HndLatB);
class HndLatC {
public:
    using priority = slabflux::priority<150>;
    void on(LatEv&, LatCtx& ctx) { ctx.sequence = (ctx.sequence * 10) + 3; }
};
EXPECT_INHERITANCE(HndLatC);

TEST(BaselineImplicitPriority, LateralFallbackIntercept) {
    LatCtx ctx;
    slabflux::core::pipeline<HndLatC, HndLatA, HndLatB> p(HndLatC{}, HndLatA{}, HndLatB{});
    LatEv eventInstance;
    p.dispatch(ctx, eventInstance);
    EXPECT_EQ(ctx.sequence, 231); 
}

// ============================================================================
// BASELINE INTEGRATION 5: TOPOLOGICAL STRESS
// ============================================================================
namespace topological_stress_test {
    struct TopoCtx { int sequence = 0; };
    class TopoRoot { public: using parents = slabflux::typelist<>; using priority = slabflux::priority<10>; virtual ~TopoRoot() = default; };
    EXPECT_INHERITANCE(TopoRoot);
    class TopoLeft : public virtual TopoRoot { public: using parents = slabflux::typelist<TopoRoot>; using priority = slabflux::priority<20>; };
    EXPECT_INHERITANCE(TopoLeft, TopoRoot);
    class TopoRight : public virtual TopoRoot { public: using parents = slabflux::typelist<TopoRoot>; using priority = slabflux::priority<30>; };
    EXPECT_INHERITANCE(TopoRight, TopoRoot);
    class TopoDiamond : public TopoLeft, public TopoRight { public: using parents = slabflux::typelist<TopoLeft, TopoRight>; using priority = slabflux::priority<40>; };
    EXPECT_INHERITANCE(TopoDiamond, TopoLeft, TopoRight);
}
REGISTER_CONTEXT(topological_stress_test::TopoRoot, topological_stress_test::TopoCtx)
REGISTER_CONTEXT(topological_stress_test::TopoLeft, topological_stress_test::TopoCtx)
REGISTER_CONTEXT(topological_stress_test::TopoRight, topological_stress_test::TopoCtx)
REGISTER_CONTEXT(topological_stress_test::TopoDiamond, topological_stress_test::TopoCtx)

namespace topological_stress_test {
    class HndTopoRoot { public: using parents = slabflux::typelist<>; using priority = slabflux::priority<10>; void on(TopoRoot&, TopoCtx& ctx) { ctx.sequence = (ctx.sequence * 10) + 1; } };
    EXPECT_INHERITANCE(HndTopoRoot);
    class HndTopoLeft : public virtual HndTopoRoot { public: using parents = slabflux::typelist<HndTopoRoot>; using priority = slabflux::priority<20>; void on(TopoLeft&, TopoCtx& ctx) { ctx.sequence = (ctx.sequence * 10) + 2; } };
    EXPECT_INHERITANCE(HndTopoLeft, HndTopoRoot);
    class HndTopoRight : public virtual HndTopoRoot { public: using parents = slabflux::typelist<HndTopoRoot>; using priority = slabflux::priority<30>; void on(TopoRight&, TopoCtx& ctx) { ctx.sequence = (ctx.sequence * 10) + 3; } };
    EXPECT_INHERITANCE(HndTopoRight, HndTopoRoot);
    class HndTopoDiamond : public HndTopoLeft, public HndTopoRight { public: using parents = slabflux::typelist<HndTopoLeft, HndTopoRight>; using priority = slabflux::priority<40>; void on(TopoDiamond&, TopoCtx& ctx) { ctx.sequence = (ctx.sequence * 10) + 4; } };
    EXPECT_INHERITANCE(HndTopoDiamond, HndTopoLeft, HndTopoRight);
}

TEST(BaselineTopologicalStress, DiamondSortLexicography) {
    topological_stress_test::TopoCtx ctx;
    slabflux::core::pipeline<topological_stress_test::HndTopoDiamond> p(topological_stress_test::HndTopoDiamond{});
    topological_stress_test::TopoDiamond eventInstance;
    p.dispatch(ctx, eventInstance);
    EXPECT_EQ(ctx.sequence, 1234);
}

namespace topological_stress_test {
    struct InvCtx { int exec_val = 0; };
    class InvBase { public: using parents = slabflux::typelist<>; using priority = slabflux::priority<10>; virtual ~InvBase() = default; };
    EXPECT_INHERITANCE(InvBase);
    class InvLeaf : public InvBase { public: using parents = slabflux::typelist<InvBase>; using priority = slabflux::priority<20>; };
    EXPECT_INHERITANCE(InvLeaf, InvBase);
    class InvLeafParallel : public InvBase { public: using parents = slabflux::typelist<InvBase>; using priority = slabflux::priority<30>; };
    EXPECT_INHERITANCE(InvLeafParallel, InvBase);
}
REGISTER_CONTEXT(topological_stress_test::InvBase, topological_stress_test::InvCtx)
REGISTER_CONTEXT(topological_stress_test::InvLeaf, topological_stress_test::InvCtx)
REGISTER_CONTEXT(topological_stress_test::InvLeafParallel, topological_stress_test::InvCtx)

namespace topological_stress_test {
    class HndInvBase { public: using parents = slabflux::typelist<>; using priority = slabflux::priority<99>; void on(InvBase&, InvCtx& ctx) { ctx.exec_val += 1; } };
    EXPECT_INHERITANCE(HndInvBase);
    class HndInvLeaf : public HndInvBase { public: using parents = slabflux::typelist<HndInvBase>; using priority = slabflux::priority<50>; void on(InvLeaf&, InvCtx& ctx) { ctx.exec_val += 10; } };
    EXPECT_INHERITANCE(HndInvLeaf, HndInvBase);
    class HndInvLeafParallel : public HndInvBase { public: using parents = slabflux::typelist<HndInvBase>; using priority = slabflux::priority<10>; void on(InvLeafParallel&, InvCtx& ctx) { ctx.exec_val += 100; } };
    EXPECT_INHERITANCE(HndInvLeafParallel, HndInvBase);
}

TEST(BaselineTopologicalStress, InverseTopologySortLaw) {
    topological_stress_test::InvCtx ctx;
    slabflux::core::pipeline<topological_stress_test::HndInvLeaf, topological_stress_test::HndInvLeafParallel> p(topological_stress_test::HndInvLeaf{}, topological_stress_test::HndInvLeafParallel{});
    topological_stress_test::InvLeaf eventInstance;
    p.dispatch(ctx, eventInstance);
    EXPECT_EQ(ctx.exec_val, 11);
}

// ============================================================================
// BASELINE INTEGRATION 6: SPORADIC SPIDER GRAPH
// ============================================================================
namespace sporadic_priority_test {
    struct SporadicCtx { std::string path = ""; };
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
}
REGISTER_CONTEXT(sporadic_priority_test::SpRoot, sporadic_priority_test::SporadicCtx)
REGISTER_CONTEXT(sporadic_priority_test::SpA, sporadic_priority_test::SporadicCtx)
REGISTER_CONTEXT(sporadic_priority_test::SpB, sporadic_priority_test::SporadicCtx)
REGISTER_CONTEXT(sporadic_priority_test::SpC, sporadic_priority_test::SporadicCtx)
REGISTER_CONTEXT(sporadic_priority_test::SpD, sporadic_priority_test::SporadicCtx)
REGISTER_CONTEXT(sporadic_priority_test::SpE, sporadic_priority_test::SporadicCtx)
REGISTER_CONTEXT(sporadic_priority_test::SpF, sporadic_priority_test::SporadicCtx)
REGISTER_CONTEXT(sporadic_priority_test::SpLeaf, sporadic_priority_test::SporadicCtx)
REGISTER_CONTEXT(sporadic_priority_test::SpTail, sporadic_priority_test::SporadicCtx)

namespace sporadic_priority_test {
    class HndSpRoot { public: using parents = slabflux::typelist<>; void on(SpRoot&, SporadicCtx& ctx) { ctx.path += "R_"; } };
    EXPECT_INHERITANCE(HndSpRoot);
    class HndSpA : public virtual HndSpRoot { public: using parents = slabflux::typelist<HndSpRoot>; void on(SpA&, SporadicCtx& ctx) { ctx.path += "A_"; } };
    EXPECT_INHERITANCE(HndSpA, HndSpRoot);
    class HndSpB : public virtual HndSpRoot { public: using parents = slabflux::typelist<HndSpRoot>; using priority = slabflux::priority<10>; void on(SpB&, SporadicCtx& ctx) { ctx.path += "B_"; } };
    EXPECT_INHERITANCE(HndSpB, HndSpRoot);
    class HndSpC : public virtual HndSpRoot { public: using parents = slabflux::typelist<HndSpRoot>; using priority = slabflux::priority<20>; void on(SpC&, SporadicCtx& ctx) { ctx.path += "C_"; } };
    EXPECT_INHERITANCE(HndSpC, HndSpRoot);
    class HndSpD : public virtual HndSpA { public: using parents = slabflux::typelist<HndSpA>; void on(SpD&, SporadicCtx& ctx) { ctx.path += "D_"; } };
    EXPECT_INHERITANCE(HndSpD, HndSpA);
    class HndSpE : public virtual HndSpB { public: using parents = slabflux::typelist<HndSpB>; using priority = slabflux::priority<5>; void on(SpE&, SporadicCtx& ctx) { ctx.path += "E_"; } };
    EXPECT_INHERITANCE(HndSpE, HndSpB);
    class HndSpF : public virtual HndSpC { public: using parents = slabflux::typelist<HndSpC>; void on(SpF&, SporadicCtx& ctx) { ctx.path += "F_"; } };
    EXPECT_INHERITANCE(HndSpF, HndSpC);
    class HndSpLeaf : public HndSpD, public HndSpE, public HndSpF { public: using parents = slabflux::typelist<HndSpD, HndSpE, HndSpF>; void on(SpLeaf&, SporadicCtx& ctx) { ctx.path += "Leaf_"; } };
    EXPECT_INHERITANCE(HndSpLeaf, HndSpD, HndSpE, HndSpF);
    class HndSpTail : public HndSpLeaf { public: using parents = slabflux::typelist<HndSpLeaf>; void on(SpTail&, SporadicCtx& ctx) { ctx.path += "Tail"; } };
    EXPECT_INHERITANCE(HndSpTail, HndSpLeaf);
}

TEST(BaselineSporadicPriority, SparseSpider) {
    sporadic_priority_test::SporadicCtx ctx;
    slabflux::core::pipeline<sporadic_priority_test::HndSpTail> dispatcher(sporadic_priority_test::HndSpTail{});
    sporadic_priority_test::SpTail event;
    dispatcher.dispatch(ctx, event);
    
    // Ensures the dispatcher correctly infers topological precedence as the 
    // primary skeleton, dynamically falling back to explicit priorities for 
    // lateral ties, and gracefully bypassing missing priorities.
    EXPECT_EQ(ctx.path, "R_A_B_C_D_E_F_Leaf_Tail");
}
