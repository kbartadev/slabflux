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
 * ============================================================================* @file pipeline_semantics.cpp
 * @brief Behavioral unit tests for pipeline execution order and short-circuit mechanics.
 * * Verifies that the C++17 fold-expression guarantees exact handler mutation order
   * and checks strict short-circuiting logic when events are consumed via scoped_ptr.
 */

#include <gtest/gtest.h>

#include "slabflux/core.hpp"

using namespace slabflux;

struct massive_event {};
EXPECT_INHERITANCE(massive_event);

struct math_event {
    int value = 0;
};
EXPECT_INHERITANCE(math_event);

// Handler 1: adds +5
struct adder {
    void on(math_event* ev) { ev->value += 5; }
};
EXPECT_INHERITANCE(adder);

// Handler 2: multiplies by 2
struct multiplier {
    void on(math_event* ev) { ev->value *= 2; }
};
EXPECT_INHERITANCE(multiplier);

// Handler 3: expects a different event type — must NEVER be invoked
struct dummy {
    void on(massive_event*) { FAIL() << "Type-system routing failed!"; }
};
EXPECT_INHERITANCE(dummy);

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
    pipeline pipe(add, dum, mult);

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
        bool on(math_event*) {
            return true; // Halts the pipeline
        }
    };

    struct panicker {
        void on(math_event*) { FAIL() << "Executed after consumption!"; }
    };

    consumer cons;
    panicker pan;
    pipeline pipe(cons, pan);

    auto ev = p.make();
    pipe.dispatch(ev);  // panicker must never execute.

    SUCCEED();
}
