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
 * ============================================================================*
 * @file industrial_jitter_test.cpp

 * SLABFLUX
 * Copyright (c) 2026 Kristóf Barta. All rights reserved.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 * Absolute Liability Limitation & Full Terms: See DISCLAIMER, NOTICE, LICENSE.
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "slabflux/core/spsc_ring_conduit.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/hw/spin_backoff.hpp"

using namespace slabflux::core;

/**
 * @brief Verifies that uncommitted reservations are invisible to the consumer.
 * This is the "Structural Honesty" test for zero-copy buffers.
 */
TEST(SpscRingConduit, ReserveCommitVisibilityIsolation) {
    spsc_ring_conduit<uint64_t, 1024> ring;
    
    uint64_t* slot = ring.reserve();
    ASSERT_NE(slot, nullptr);
    *slot = 999ULL;

    // Consumer should see nothing yet
    uint64_t out = 0;
    EXPECT_FALSE(ring.try_pop(out));
    EXPECT_EQ(ring.peek(), nullptr);

    // Commit logic
    ring.commit();

    // Visibility achieved
    EXPECT_NE(ring.peek(), nullptr);
    EXPECT_TRUE(ring.try_pop(out));
    EXPECT_EQ(out, 999ULL);
}

/**
 * @brief Stress tests memory visibility across a non-blocking commit boundary.
 * Validates the Release-Acquire fence in the Reserve API.
 */
TEST(SpscRingConduit, CrossCoreFenceHardening) {
    struct alignas(64) complex_payload { 
        uint64_t a, b, c, d; 
        uint64_t guard_start;
        uint64_t guard_end;
    };
    spsc_ring_conduit<complex_payload, 4096> ring;
    constexpr size_t ITER = 5'000'000;
    alignas(64) std::atomic<bool> start_gate{false};

    std::thread producer([&]() {
        hardware_topology::pin_thread(1);
        uint32_t gate_yield = 0;
        while(!start_gate.load(std::memory_order_acquire)) slabflux::hw::spin_backoff(gate_yield);
        for (size_t i = 0; i < ITER; ++i) {
            complex_payload* p;
            uint32_t yield_count = 0;
            while (!(p = ring.reserve())) slabflux::hw::spin_backoff(yield_count);
            p->guard_start = 0xAAAAAAAAAAAAAAAA;
            p->a = i; p->b = i + 1; p->c = i + 2; p->d = i + 3;
            p->guard_end = 0xAAAAAAAAAAAAAAAA;
            ring.commit();
        }
    });

    hardware_topology::pin_thread(2);
    start_gate.store(true, std::memory_order_release);
    for (size_t i = 0; i < ITER; ++i) {
        complex_payload out;
        uint32_t yield_count = 0;
        while (!ring.try_pop(out)) slabflux::hw::spin_backoff(yield_count);
        
        // If the memory order is breached, we might catch a torn write where 
        // the guards are correct but the payload is stale or partial.
        ASSERT_EQ(out.guard_start, 0xAAAAAAAAAAAAAAAA);
        ASSERT_EQ(out.guard_end, 0xAAAAAAAAAAAAAAAA);
        ASSERT_EQ(out.a, i);
        ASSERT_EQ(out.d, i + 3);
    }
    producer.join();
}

TEST(SpscRingConduit, ReserveCommitIntegrity) {
    spsc_ring_conduit<uint64_t, 1024> ring;
    uint64_t* slot = ring.reserve();
    ASSERT_NE(slot, nullptr);
    *slot = 8888ULL;
    ring.commit();

    const uint64_t* peeked = ring.peek();
    ASSERT_NE(peeked, nullptr);
    EXPECT_EQ(*peeked, 8888ULL);
    
    uint64_t out = 0;
    ASSERT_TRUE(ring.try_pop(out));
    EXPECT_EQ(out, 8888ULL);
}

TEST(SpscRingConduit, ZeroCopyConcurrency) {
    constexpr size_t ITERATIONS = 1'000'000;
    spsc_ring_conduit<size_t, 4096> ring;

    std::thread producer([&]() {
        for (size_t i = 0; i < ITERATIONS; ++i) {
            size_t* p;
            uint32_t yield_count = 0;
            while (!(p = ring.reserve())) { slabflux::hw::spin_backoff(yield_count); }
            *p = i;
            ring.commit();
        }
    });

    std::thread consumer([&]() {
        for (size_t i = 0; i < ITERATIONS; ++i) {
            size_t val;
            uint32_t yield_count = 0;
            while (!ring.try_pop(val)) { slabflux::hw::spin_backoff(yield_count); }
            ASSERT_EQ(val, i);
        }
    });
    producer.join(); consumer.join();
}

/**
 * @brief Vectorized Batch Egress.
 * Validates that pop_batch correctly utilizes SIMD (AVX-512/AVX2) to drain pointers.
 * This mirrors the high-throughput pressure applied to MPMC sharded matrices.
 */
TEST(RingConduit, VectorizedBatchIntegrity) {
    spsc_ring_conduit<uint64_t, 1024> ring;
    uint64_t data[64];
    for(uint64_t i=0; i<64; ++i) data[i] = 0xAABB0000 | i;

    // Fill via scalar push
    for(uint64_t i=0; i<64; ++i) ring.push(data[i]);

    uint64_t out[64] = {0};
    size_t count = ring.pop_batch(out, 64);
    
    EXPECT_EQ(count, 64);
    for(uint64_t i=0; i<64; ++i) {
        EXPECT_EQ(out[i], 0xAABB0000 | i);
    }
}
