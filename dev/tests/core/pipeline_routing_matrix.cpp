/**
 * @file pipeline_routing_matrix.cpp
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

// ---------------------------------------------------------
// 3 DIFFERENT HANDLER TYPES (For separate pipelines)
// ---------------------------------------------------------

struct HandlerRef {
    int calls = 0;
    bool on(matrix_event& ev) noexcept {
        calls++;
        ev.payload++;
        return true;
    }
};

struct HandlerScoped {
    int calls = 0;
    bool stolen = false;

    bool on(matrix_event& ev) noexcept {
        calls++;
        ev->payload++;

        stolen = true;
        return true;
    }
};

// ---------------------------------------------------------
// 3×3 ROUTING MATRIX TESTS
// ---------------------------------------------------------

// --- INPUT 1: scoped_ptr (The universal input type) ---
TEST(RoutingMatrix, Input_ScopedPtr_Can_Route_To_All_Three) {
    HandlerRef h_ref;
    HandlerScoped h_scoped;

    pipeline pipe_ref(h_ref);
    pipeline pipe_scoped(h_scoped);

    matrix_event ev1;
    scoped_ptr<matrix_event> sp1(&ev1, nullptr, nullptr);

    // 2. Route to Ref
    matrix_event ev2;
    scoped_ptr<matrix_event> sp2(&ev2, nullptr, nullptr);
    pipe_ref.dispatch(sp2);
    EXPECT_EQ(h_ref.calls, 1);

    // 3. Route to Scoped (Test ownership theft)
    matrix_event ev3;
    scoped_ptr<matrix_event> sp3(&ev3, nullptr, nullptr);
    EXPECT_TRUE(sp3); // Still holds ownership

    pipe_scoped.dispatch(sp3);

    EXPECT_EQ(h_scoped.calls, 1);
    EXPECT_TRUE(h_scoped.stolen);
    EXPECT_FALSE(sp3); // Ownership successfully stolen
}

// --- INPUT 3: Lvalue Reference ---
TEST(RoutingMatrix, Input_LvalueRef_Routes_To_Ref) {
    HandlerRef h_ref;
    pipeline pipe_ref(h_ref);

    matrix_event ev;

    pipe_ref.dispatch(ev);
    EXPECT_EQ(h_ref.calls, 1);
}
