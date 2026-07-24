/**
 * @file pipeline.cpp
 * @brief Comprehensive SLABFLUX V2 pipeline test suite.
 *
 * This file validates:
 *  - 4D Matrix Dispatch (multi-level inheritance + multi-handler resolution)
 *  - Ephemeral Context Engine (fresh L1 stack context per dispatch)
 *  - Context Aggregation (multiple handlers mutating the same context block)
 *  - Heterogeneous Context Tuples (multi-context dispatch for one event)
 *  - Context Inheritance (base-layer upcasting correctness)
 *  - Chicago Gateway Demuxer (branchless O(1) network event routing)
 *  - SPSC/MPSC Conduits and Asymmetric Pool edge cases
 *
 * These tests collectively prove that the SLABFLUX pipeline:
 *  - performs correct overload resolution across all handler signatures,
 *  - maintains deterministic context lifetime and isolation,
 *  - supports multi-context and inherited-context dispatch,
 *  - and integrates cleanly with the zero-copy network demuxer.
 */

#include <gtest/gtest.h>
#include <cassert>
#include <iostream>
#include <vector>
#include <string>
#include <tuple>
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

#include "slabflux/core/pipeline.hpp"
#include "slabflux/core/conduit.hpp"
#include "slabflux/core/pool.hpp"
#include "slabflux/net/gateway.hpp"

struct tracker_t {
    std::vector<std::string> log;
    void push(const std::string& s) { log.push_back(s); }
    void reset() { log.clear(); }
} g_tracker;

// ----------------------------------------------------------------------------
// TEST 1: PURE INHERITANCE EXECUTION PROOF (4D Matrix)
// ----------------------------------------------------------------------------
struct base_risk_ctx { int risk_level = 0; };
struct trading_ctx : public base_risk_ctx { int shared_state = 0; };

struct A : {};
struct B : public A { double price; };
struct C : public A { double bid; double ask; };
struct D : { double bid; double ask; };

struct Ctx_A : public trading_ctx { using slabflux_exclusive_event = A; };
struct Ctx_B : public trading_ctx { using slabflux_exclusive_event = B; };
struct Ctx_C : public trading_ctx { using slabflux_exclusive_event = C; };
struct Ctx_D : public trading_ctx { using slabflux_exclusive_event = D; };

namespace slabflux::meta {
    template <> struct event_context_map<A> { using type = Ctx_A; };
    template <> struct event_context_map<B> { using type = Ctx_B; };
    template <> struct event_context_map<C> { using type = Ctx_C; };
    template <> struct event_context_map<D> { using type = Ctx_D; };
}

struct RiskManager {
    void on(const A&, base_risk_ctx& ctx) { g_tracker.push("A"); ctx.risk_level += 1; }
    void on(const B&, base_risk_ctx&)     { g_tracker.push("B"); }
    void on(const C&, trading_ctx& ctx)   { g_tracker.push("C"); ctx.shared_state += 10; }
    void on(const D&, trading_ctx&)       { g_tracker.push("D"); }
};

struct AlphaModel : public RiskManager {
    void on(const A&, trading_ctx& ctx)   { g_tracker.push("A1"); ctx.shared_state += 10; }
    void on(const B&, trading_ctx&)       { g_tracker.push("B1"); }
    void on(const C&, trading_ctx& ctx)   { g_tracker.push("C1"); ctx.risk_level += 1; }
    void on(const D&, trading_ctx&)       { g_tracker.push("D1"); }
};

struct BetaModel {
    void on(const A&, base_risk_ctx& ctx) { g_tracker.push("BA"); ctx.risk_level += 1; }
    void on(const B&, base_risk_ctx&)     { g_tracker.push("BB"); }
    void on(const C&, trading_ctx& ctx)   { g_tracker.push("BC"); ctx.shared_state += 10; }
    void on(const D&, trading_ctx&)       { g_tracker.push("BD"); }
};

TEST(PipelineTest, Test_4D_Execution) {
    AlphaModel alpha;
    BetaModel beta;
    slabflux::core::pipeline matrix(alpha, beta);

    g_tracker.reset();
    A a_ev; matrix.dispatch(a_ev);
    EXPECT_EQ(g_tracker.log.size(), 3);
    EXPECT_EQ(g_tracker.log[0], "A");
    EXPECT_EQ(g_tracker.log[1], "A1");
    EXPECT_EQ(g_tracker.log[2], "BA");

    g_tracker.reset();
    B b_ev; matrix.dispatch(b_ev);
    EXPECT_EQ(g_tracker.log.size(), 6);
    EXPECT_EQ(g_tracker.log[0], "A");
    EXPECT_EQ(g_tracker.log[1], "B");
    EXPECT_EQ(g_tracker.log[2], "A1");
    EXPECT_EQ(g_tracker.log[3], "B1");
    EXPECT_EQ(g_tracker.log[4], "BA");
    EXPECT_EQ(g_tracker.log[5], "BB");

    g_tracker.reset();
    C c_ev; matrix.dispatch(c_ev);
    EXPECT_EQ(g_tracker.log.size(), 6);
    EXPECT_EQ(g_tracker.log[0], "A");
    EXPECT_EQ(g_tracker.log[1], "C");
    EXPECT_EQ(g_tracker.log[2], "A1");
    EXPECT_EQ(g_tracker.log[3], "C1");
    EXPECT_EQ(g_tracker.log[4], "BA");
    EXPECT_EQ(g_tracker.log[5], "BC");

    g_tracker.reset();
    D d_ev; matrix.dispatch(d_ev);
    EXPECT_EQ(g_tracker.log.size(), 3);
    EXPECT_EQ(g_tracker.log[0], "D");
    EXPECT_EQ(g_tracker.log[1], "D1");
    EXPECT_EQ(g_tracker.log[2], "BD");

    //std::cout << "[OK] TEST 2: 4D Matrix Execution flawless.\n";
}

// ----------------------------------------------------------------------------
// TEST 2: CHICAGO GATEWAY & EPHEMERAL CONTEXT
// ----------------------------------------------------------------------------
struct Ev_Mapped { double value; };
struct Ev_NoMap { int flag; };

struct RuleContext {
    //using slabflux_exclusive_event = Ev_Mapped;
    int state = 0;
};
/*
namespace slabflux::meta {
    template <> struct event_context_map<Ev_Mapped> { using type = RuleContext; };
}
*/
using my_execution_graph = slabflux::dispatch_topology<
RuleContext (Ev_Mapped),
>;

struct GatewayHandler {
    void on(const Ev_Mapped& ev, RuleContext& ctx) {
        g_tracker.push("Mapped:" + std::to_string((int)ev.value));
        ctx.state = 99;
    }
    void on(const Ev_NoMap& ev) {
        g_tracker.push("NoMap:" + std::to_string(ev.flag));
    }
};

struct EphemeralChecker {
    void on(const Ev_Mapped&, RuleContext& ctx) {
        EXPECT_EQ(ctx.state, 0); // fresh stack context
    }
    void on(const Ev_NoMap&) {}
};

TEST(PipelineTest, Test_Gateway_And_Rules) {
    GatewayHandler h;
    EphemeralChecker checker;

    using my_pipeline_t = slabflux::core::pipeline<EphemeralChecker, GatewayHandler>;
    my_pipeline_t matrix(checker, h);

    slabflux::net::chicago_gateway<my_pipeline_t> demuxer;
    demuxer.bind<Ev_Mapped>();
    demuxer.bind<Ev_NoMap>();

    // 1st packet
    alignas(8) char buffer1[16] = {0};
    *reinterpret_cast<uint16_t*>(buffer1) = 111;
    *reinterpret_cast<double*>(buffer1 + 8) = 42.0;

    g_tracker.reset();
    demuxer.on_network_bytes_received(buffer1, matrix);
    ASSERT_EQ(g_tracker.log.size(), 1);
    EXPECT_EQ(g_tracker.log[0], "Mapped:42");

    // 2nd packet: context must be fresh again
    alignas(8) char buffer1_b[16] = {0};
    *reinterpret_cast<uint16_t*>(buffer1_b) = 111;
    *reinterpret_cast<double*>(buffer1_b + 8) = 43.0;

    demuxer.on_network_bytes_received(buffer1_b, matrix);
    ASSERT_EQ(g_tracker.log.size(), 2);
    EXPECT_EQ(g_tracker.log[1], "Mapped:43");

    //std::cout << "[OK] TEST 1: Chicago Gateway & Ephemeral Rules flawless.\n";
}

// ----------------------------------------------------------------------------
// TEST 3: CONDUIT & POOL EDGE CASES
// ----------------------------------------------------------------------------
struct massive_event { char data[4096]; };

TEST(PipelineTest, Test_Conduits) {
    slabflux::core::ring_conduit<int, 4> conduit;
    EXPECT_EQ(conduit.peek(), nullptr);

    for (int i = 0; i < 4; ++i) {
        int* s = conduit.reserve();
        *s = i;
        conduit.commit();
    }

    EXPECT_EQ(conduit.reserve(), nullptr);

    for (int i = 0; i < 4; ++i)
        conduit.consume();

    int* w = conduit.reserve();
    *w = 99;
    conduit.commit();
    EXPECT_EQ(*conduit.peek(), 99);

    slabflux::core::mpsc_pool<massive_event, 2> pool;
    massive_event* e1 = pool.make_raw();
    massive_event* e2 = pool.make_raw();
    EXPECT_EQ(pool.make_raw(), nullptr);

    pool.release(e1);
    pool.release(e2);
    EXPECT_EQ(pool.make_raw(), nullptr);

    pool.reclaim_returns();
    EXPECT_NE(pool.make_raw(), nullptr);

    //std::cout << "[OK] TEST 3: SPSC Conduit & Asymmetric Pool flawless.\n";
}

TEST(PipelineTest, Test_Pool_Automatic_Reclaim) {
    slabflux::core::mpsc_pool<massive_event, 2, slabflux::core::reclaim_strategy::automatic> pool;
    massive_event* e1 = pool.make_raw();
    massive_event* e2 = pool.make_raw();
    EXPECT_EQ(pool.make_raw(), nullptr);

    pool.release(e1);
    pool.release(e2);

    // Automatic reclaim should happen inside make_raw()
    massive_event* e3 = pool.make_raw();
    EXPECT_NE(e3, nullptr);
    EXPECT_NE(pool.make_raw(), nullptr);
    EXPECT_EQ(pool.make_raw(), nullptr);
}

// TEST 4: EPHEMERAL CONTEXT ISOLATION & AGGREGATION
struct SharedEvent { int id; SharedEvent(int i) : id(i) {} };
struct SharedCtx {
    using slabflux_exclusive_event = SharedEvent;
    int call_count = 0;
};

namespace slabflux::meta {
    template <> struct event_context_map<SharedEvent> { using type = SharedCtx; };
}

struct ContextValidator {
    // Both handlers will be called in the same pipeline dispatch.
    // We verify they receive the SAME context instance.
    void on(const SharedEvent&, SharedCtx& ctx) {
        ctx.call_count += 1;
    }
};

struct SecondValidator {
    void on(const SharedEvent&, SharedCtx& ctx) {
        // Verify aggregation: should be 1, because ContextValidator ran first
        EXPECT_EQ(ctx.call_count, 1);
        ctx.call_count += 10;
    }
};

TEST(PipelineTest, Test_Context_Aggregation_And_Isolation) {
    ContextValidator h1;
    SecondValidator h2;
    slabflux::core::pipeline pipe(h1, h2);

    // Dispatch 1
    SharedEvent ev1{1};
    pipe.dispatch(ev1);

    // Check if the final state matches the sum of increments (1 + 10)
    // We can't easily access the internal context here, so we verify by
    // adding a final checker or using a global tracker if needed.
    // Or, more simply, use a Mock/Observer:

    struct Observer {
        int final_val = 0;
        void on(const SharedEvent&, SharedCtx& ctx) { final_val = ctx.call_count; }
    } obs;

    slabflux::core::pipeline pipe2(h1, h2, obs);
    pipe2.dispatch(ev1);
    EXPECT_EQ(obs.final_val, 11);

    // Dispatch 2: Verify Isolation (ensure call_count starts at 0 again)
    pipe2.dispatch(ev1);
    EXPECT_EQ(obs.final_val, 11);
}

// 1. Define the event
struct DualEvent {
    int payload = 42;
};

// 2. Define the TWO DIFFERENT contexts
// (slabflux_exclusive_event is required because of the static_assert)
struct AlphaCtx {
    using slabflux_exclusive_event = DualEvent;
    int a_calls = 0;
};

struct BetaCtx {
    using slabflux_exclusive_event = DualEvent;
    int b_calls = 0;
};

// 3. Map the event to a Tuple containing both contexts
namespace slabflux::meta {
    template <> struct event_context_map<DualEvent> {
        using type = std::tuple<AlphaCtx, BetaCtx>;
    };
}

// 4. The HANDLER with two different 'on' overloads for the same event
struct DualContextHandler {
    void on(const DualEvent& ev, AlphaCtx& ctx) {
        EXPECT_EQ(ev.payload, 42); // Just to ensure correct data arrives
        ctx.a_calls++;
    }

    void on(const DualEvent& ev, BetaCtx& ctx) {
        EXPECT_EQ(ev.payload, 42);
        ctx.b_calls++;
    }
};

// 5. A VALIDATOR handler that reads the contexts at the end of the pipeline,
// since contexts are L1 stack-allocated and destroyed after dispatch().
struct DualContextValidator {
    int final_a = 0;
    int final_b = 0;

    void on(const DualEvent&, AlphaCtx& ctx) {
        final_a = ctx.a_calls;
    }
    void on(const DualEvent&, BetaCtx& ctx) {
        final_b = ctx.b_calls;
    }
};

// 6. THE TEST ITSELF
TEST(PipelineTest, HeterogeneousContextDispatch_TwoOnsInOneHandler) {
    DualContextHandler handler;
    DualContextValidator validator;

    // Build the pipeline
    slabflux::core::pipeline pipe(handler, validator);

    // Fire the event
    DualEvent ev;
    bool handled = pipe.dispatch(ev);

    // Checks
    EXPECT_TRUE(handled) << "Pipeline did not report successful dispatch!";
    EXPECT_EQ(validator.final_a, 1) << "AlphaCtx overload DID NOT run!";
    EXPECT_EQ(validator.final_b, 1) << "BetaCtx overload DID NOT run!";
}

// ============================================================================
// SINGLE EVENT, INHERITED CONTEXT MIXINS
// ============================================================================

// 1. Single distinct event
struct SingleMarginEvent {
    double maintenance_margin = 15000.0;
};

// 2. Separate base context mixins defining distinct domains
struct BaseRiskState {
    using slabflux_exclusive_event = SingleMarginEvent;
    int risk_checks_failed = 0;
};

struct BaseAuditState {
    using slabflux_exclusive_event = SingleMarginEvent;
    bool log_sealed = false;
};

// 3. The true structural context inheriting from multiple base states
// Allocated as ONE single layout block on the L1 stack.
struct DerivedEngineContext : public BaseRiskState, public BaseAuditState {
    using slabflux_exclusive_event = SingleMarginEvent;
    int processing_core = 2;
};

// Map the Event 1:1 to the singular derived context
namespace slabflux::meta {
    template <>
    struct event_context_map<SingleMarginEvent> {
        using type = DerivedEngineContext;
    };
}

// ============================================================================
// HANDLERS OVERLOADING ON THE CONTEXT INHERITANCE LAYER
// ============================================================================
struct ContextInheritanceHandler {
    // Target BaseRiskState layer via implicit upcasting
    void on(const SingleMarginEvent&, BaseRiskState& risk_ctx) {
        risk_ctx.risk_checks_failed += 1;
    }

    // Target BaseAuditState layer via implicit upcasting
    void on(const SingleMarginEvent&, BaseAuditState& audit_ctx) {
        audit_ctx.log_sealed = true;
    }
};

// Collector to verify modifications hit the same memory block
struct ContextInheritanceValidator {
    int final_risk_failed = 0;
    bool final_audit_sealed = false;

    void on(const SingleMarginEvent&, DerivedEngineContext& ctx) {
        final_risk_failed = ctx.risk_checks_failed;
        final_audit_sealed = ctx.log_sealed;
    }
};

// ============================================================================
// VERIFICATION PROOF
// ============================================================================
TEST(PipelineTest, Test_Context_Inheritance_Native_Upcast) {
    ContextInheritanceHandler handler;
    ContextInheritanceValidator validator;

    // Using your original, clean pipeline instance
    slabflux::core::pipeline pipe(handler, validator);

    SingleMarginEvent ev;
    bool handled = pipe.dispatch(ev);

    EXPECT_TRUE(handled);

    // Verify C++ overload resolution routed the single context reference
    // to both separate base-layer 'on' methods correctly.
    EXPECT_EQ(validator.final_risk_failed, 1)
        << "Compilation passed but context base-layer memory state mutated incorrectly!";
    EXPECT_TRUE(validator.final_audit_sealed)
        << "Compilation passed but second context base-layer failed to execute!";
}
