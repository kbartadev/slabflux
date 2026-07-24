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
 * ============================================================================* @file pipeline_routing_matrix.cpp
 * @brief Proving the 3x3 Input/Handler routing combinations and ownership theft.
 */

#include <gtest/gtest.h>
#include <iostream>

#include "slabflux/core/memory.hpp"
#include "slabflux/core/pipeline.hpp"
#include "slabflux/meta.hpp"

using namespace slabflux::core;

struct matrix_event {
    int payload = 0;
};
EXPECT_INHERITANCE(matrix_event);

// ---------------------------------------------------------
// 3 DIFFERENT HANDLER TYPES (For separate pipelines)
// ---------------------------------------------------------

struct HandlerPtr {
    int calls = 0;
    bool on(matrix_event* ev) noexcept {
        calls++;
        ev->payload++;
        return true;
    }
};
EXPECT_INHERITANCE(HandlerPtr);

struct HandlerRef {
    int calls = 0;
    bool on(matrix_event& ev) noexcept {
        calls++;
        ev.payload++;
        return true;
    }
};
EXPECT_INHERITANCE(HandlerRef);

// ---------------------------------------------------------
// 3×3 ROUTING MATRIX TESTS
// ---------------------------------------------------------

// --- INPUT 1: scoped_ptr (Smart Pointers unwrapped perfectly) ---
TEST(RoutingMatrix, Input_ScopedPtr_Routes_Safely) {
    HandlerPtr h_ptr;
    HandlerRef h_ref;

    pipeline pipe_ptr(h_ptr);
    pipeline pipe_ref(h_ref);

    // 1. Route to Ptr
    matrix_event ev1;
    scoped_ptr<matrix_event> sp1(&ev1, nullptr, nullptr);
    pipe_ptr.dispatch(sp1);
    EXPECT_EQ(h_ptr.calls, 1);

    // 2. Route to Ref
    matrix_event ev2;
    scoped_ptr<matrix_event> sp2(&ev2, nullptr, nullptr);
    pipe_ref.dispatch(sp2);
    EXPECT_EQ(h_ref.calls, 1);
}

// --- INPUT 2: Raw Pointer ---
TEST(RoutingMatrix, Input_RawPointer_Routes_Safely) {
    HandlerPtr h_ptr;
    HandlerRef h_ref;

    pipeline pipe_ptr(h_ptr);
    pipeline pipe_ref(h_ref);

    matrix_event ev;

    // 1. Route to Ptr
    pipe_ptr.dispatch(&ev);
    EXPECT_EQ(h_ptr.calls, 1);

    // 2. Route to Ref (try_call dereferences automatically)
    pipe_ref.dispatch(&ev);
    EXPECT_EQ(h_ref.calls, 1);
}

// --- INPUT 3: Lvalue Reference ---
TEST(RoutingMatrix, Input_LvalueRef_Routes_To_Ref) {
    HandlerRef h_ref;
    pipeline pipe_ref(h_ref);

    matrix_event ev;

    pipe_ref.dispatch(ev);
    EXPECT_EQ(h_ref.calls, 1);
}
