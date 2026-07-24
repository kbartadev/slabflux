/**
 * @file pipeline_semantics.cpp
 * @brief Behavioral unit tests for pipeline execution order and short-circuit mechanics.
 * * Verifies that the C++17 fold-expression guarantees exact handler mutation order
   * and checks strict short-circuiting logic when events are consumed via scoped_ptr.
 */

#include <gtest/gtest.h>

#include "slabflux/core.hpp"

using namespace slabflux;

struct massive_event {};

struct math_event {
    int value = 0;
};

// Handler 1: adds +5
struct adder {
    void on(math_event& ev) { ev.value += 5; }
};

// Handler 2: multiplies by 2
struct multiplier {
    void on(math_event& ev) { ev.value *= 2; }
};

// Handler 3: expects a different event type — must NEVER be invoked
struct dummy {
    void on(massive_event&) { FAIL() << "Type-system routing failed!"; }
};

// ============================================================================
// TEST 1 — Fold-expression semantics: mutation order must be exact.
// The pipeline must execute handlers strictly in declaration order,
// skipping handlers whose type signature does not match.
// ============================================================================
TEST(PipelineSemantics, fold_expression_maintains_exact_mutation_order) {
    pool<math_event, 10> p;
    adder add;
    multiplier mult;
    dummy dum;

    // Order: Add → Dummy (skipped) → Multiply
    pipeline<adder, dummy, multiplier> pipe(add, dum, mult);

    auto* ev = p.make_raw();
    pipe.dispatch(ev);

    // (0 + 5) * 2 = 10. Any deviation in order yields the wrong result.
    EXPECT_EQ(ev->value, 10);
}

// ============================================================================
// TEST 2 — Short-circuit semantics: consuming the event must halt the chain.
// Once a handler resets the event_ptr, no subsequent handler may run.
// ============================================================================
TEST(PipelineSemantics, pipeline_safely_short_circuits_if_event_is_consumed) {
    pool<math_event, 10> p;

    struct consumer {
        bool on(math_event& ev) {
            // The pipeline fully handles smart-pointer semantics in the background.
            // Returning true signals the pipeline to halt and discard the memory instantly.
            return true;
        }
    };

    struct panicker {
        void on(math_event&) { FAIL() << "Executed after consumption!"; }
    };

    consumer cons;
    panicker pan;
    pipeline<consumer, panicker> pipe(cons, pan);

    auto ev = p.make();
    pipe.dispatch(ev);  // panicker must never execute.

    SUCCEED();
}
