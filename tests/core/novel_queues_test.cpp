/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file novel_queues_test.cpp
 * @brief Unit tests for the newly introduced wait-free queue architectures.
 */

#include <gtest/gtest.h>
#include "slabflux/core/orthogonal_manifold.hpp"
#include "slabflux/core/cross_orthogonal_queue.hpp"
#include "slabflux/core/asymmetric_dispersion_queue.hpp"
#include "slabflux/core/pendulum_spsc_conduit.hpp"

using namespace slabflux::core;

TEST(NovelQueuesTest, OrthogonalManifold_PushPop) {
    orthogonal_manifold<int, 16> q;
    int a = 1, b = 2;
    
    EXPECT_TRUE(q.push(&a));
    EXPECT_TRUE(q.push(&b));
    
    int* p1 = q.pop();
    int* p2 = q.pop();
    
    EXPECT_NE(p1, nullptr);
    EXPECT_NE(p2, nullptr);
    
    // Orthogonal routing does not guarantee strict FIFO across threads,
    // but both items must be recovered.
    EXPECT_EQ(*p1 + *p2, 3);
    EXPECT_EQ(q.pop(), nullptr);
}

TEST(NovelQueuesTest, CrossOrthogonal_CapacityExhaustion) {
    cross_orthogonal_queue<int, 4> q; // 4 rows
    int dummy = 42;
    
    // Fill up matrix
    int successful_pushes = 0;
    for(int i = 0; i < 100; ++i) {
        if (q.push(&dummy)) successful_pushes++;
    }
    
    // Must be bounded
    EXPECT_GT(successful_pushes, 0);
    EXPECT_LT(successful_pushes, 100);
}

TEST(NovelQueuesTest, AsymmetricDispersion_BasicFlow) {
    asymmetric_dispersion_queue<int, 8> q;
    int a = 10, b = 20;
    
    EXPECT_TRUE(q.push(&a));
    EXPECT_TRUE(q.push(&b));
    
    int* out1 = q.pop();
    EXPECT_NE(out1, nullptr);
    EXPECT_EQ(*out1, 10); // Consumer sweeps linearly, so MPSC is semi-ordered per producer
}

TEST(NovelQueuesTest, PendulumSPSC_Oscillation) {
    pendulum_spsc_conduit<int, 4> q;
    int a = 1, b = 2, c = 3;
    
    EXPECT_TRUE(q.try_push(&a));
    EXPECT_TRUE(q.try_push(&b));
    EXPECT_TRUE(q.try_push(&c));
    (void)q.try_push(&a); // Exhaust remaining capacity dynamic buffers
    EXPECT_FALSE(q.try_push(&a));
    
    int* out = nullptr;
    EXPECT_TRUE(q.try_pop(out)); EXPECT_EQ(*out, 1);
    EXPECT_TRUE(q.try_pop(out)); EXPECT_EQ(*out, 2);
    EXPECT_TRUE(q.try_pop(out)); EXPECT_EQ(*out, 3);
    
    while(q.try_pop(out)) {} // Drain completely
    EXPECT_FALSE(q.try_pop(out));
}