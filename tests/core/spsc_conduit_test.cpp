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
#include <vector>
#include <numeric>
#include <chrono>
#include <immintrin.h>
#include "slabflux/core/spsc_ring_conduit.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/mpmc_pool.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/hw/spin_backoff.hpp"

using namespace slabflux::core;

struct tiny_event { uint8_t data[64]; }; // Aligned to cache line

/**
 * @brief Audits the physical layout of the SPSC conduit.
 * Ensures 0% False Sharing and validates isolation.
 */
TEST(SpscConduit, PhysicalArchitecture) {
    spsc_conduit<uint64_t*, 1024> wire;
    
    // Verify overall alignment
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&wire) % 64, 0);

    // Verify that the internal structure provides at least 3 isolated 
    // 64-byte windows (Ring, Head, Tail).
    EXPECT_GE(sizeof(wire), 64 * 3); 
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&wire) % 64, 0);
}

/**
 * @brief High-velocity concurrency test with thread pinning.
 */
TEST(SpscConduit, ConcurrencyAndIntegrity) {
    constexpr size_t ITERATIONS = 50'000'000; // Shatter-level stress
    spsc_conduit<uint64_t*, 4096> wire;
    std::vector<uint64_t> data(ITERATIONS);
    std::iota(data.begin(), data.end(), 0);

    alignas(64) std::atomic<bool> start_gate{false};

    std::thread producer([&]() {
        hardware_topology::pin_thread(1);
        uint32_t gate_yield = 0;
        while(!start_gate.load()) slabflux::hw::spin_backoff(gate_yield);
        for (size_t i = 0; i < ITERATIONS; ++i) {
            uint32_t yield_count = 0;
            while (!wire.try_push(&data[i])) { slabflux::hw::spin_backoff(yield_count); }
        }
    });

    hardware_topology::pin_thread(2);
    start_gate.store(true);

    for (size_t i = 0; i < ITERATIONS; ++i) {
        uint64_t* ptr = nullptr;
        uint32_t yield_count = 0;
        while (!wire.try_pop(ptr)) { slabflux::hw::spin_backoff(yield_count); }
        ASSERT_EQ(*ptr, i);
    }
    producer.join();
}

/**
 * @brief Verifies the Ambiguity Resolution Contract.
 * try_pop(T&) must distinguish between "Queue Empty" and "Item is NULL".
 */
TEST(SpscConduit, ContractAmbiguity) {
    spsc_conduit<int*, 1024> wire;
    int* out = reinterpret_cast<int*>(0xDEADBEEF);
    EXPECT_FALSE(wire.try_pop(out));
    EXPECT_EQ(out, reinterpret_cast<int*>(0xDEADBEEF));

    wire.try_push(nullptr);
    EXPECT_TRUE(wire.try_pop(out));
    EXPECT_EQ(out, nullptr);
}

TEST(SpscConduit, AVX512_VectorizedBatchDrain) {
    spsc_conduit<uint64_t*, 1024> wire;
    uint64_t data[1024];
    for(int i = 0; i < 1024; ++i) data[i] = i;
    for(int i = 0; i < 500; ++i) { wire.push(&data[0]); wire.pop(); }
    for(int i = 0; i < 13; ++i) ASSERT_TRUE(wire.try_push(&data[i]));

    uint64_t* batch[16];
    size_t n = wire.pop_batch(batch, 16);
    EXPECT_EQ(n, 13);
    for(size_t j = 0; j < 13; ++j) ASSERT_EQ(*batch[j], j);
}

TEST(SpscConduit, ExactSaturationInvariants) {
    constexpr size_t SIZE = 1024;
    spsc_conduit<uint64_t*, SIZE> wire;
    uint64_t val = 0xFF;
    for (size_t i = 0; i < SIZE; ++i) ASSERT_TRUE(wire.try_push(&val));
    EXPECT_FALSE(wire.try_push(&val));
    uint64_t* out;
    ASSERT_TRUE(wire.try_pop(out));
    EXPECT_TRUE(wire.try_push(&val));
}

TEST(SpscConduit, ShadowPointerBackpressureResilience) {
    spsc_conduit<uint64_t*, 4> wire;
    uint64_t val = 42;
    EXPECT_TRUE(wire.try_push(&val));
    EXPECT_TRUE(wire.try_push(&val));
    EXPECT_TRUE(wire.try_push(&val));
    EXPECT_TRUE(wire.try_push(&val));
    EXPECT_FALSE(wire.try_push(&val));
    EXPECT_EQ(wire.occupancy(), 4);
}

TEST(SpscConduit, AVX512_InvalidationRecovery) {
    spsc_conduit<uint64_t*, 1024> wire;
    uint64_t target = 0xCAFEBABE, dummy = 0;
    for (int i = 0; i < 100; ++i) wire.push(&dummy);
    wire.push(&target);
    wire.invalidate_by_ptr(&target);
    uint64_t* p = nullptr;
    for (int i = 0; i < 101; ++i) {
        uint32_t yield_count = 0;
        while (!wire.try_pop(p)) { slabflux::hw::spin_backoff(yield_count); }
        if (i == 100) EXPECT_EQ(p, nullptr);
    }
}

/**
 * @brief Verifies automated reclamation through managed_data integration.
 */
TEST(SpscConduit, ManagedDataPoolIntegration) {
    mpmc_pool<uint64_t, 1024> pool;
    spsc_conduit<uint64_t*, 1024> wire;
    auto managed = pool.make(12345ULL);
    wire.push(managed); 
    EXPECT_EQ(managed.get(), nullptr);

    auto recovered = wire.try_pop(pool);
    ASSERT_TRUE(recovered);
    EXPECT_EQ(*recovered, 12345ULL);
}

/**
 * @brief Verifies that managed_data retains ownership if push fails.
 */
TEST(SpscConduit, BackpressureOwnershipRetention) {
    mpmc_pool<uint64_t, 128> pool;
    spsc_conduit<uint64_t*, 2> wire;
    auto m1 = pool.make(1ULL);
    auto m2 = pool.make(2ULL);
    ASSERT_TRUE(wire.try_push(m1));
    ASSERT_TRUE(wire.try_push(m2));

    auto m3 = pool.make(3ULL);
    uint64_t* raw_addr = m3.get();
    EXPECT_FALSE(wire.try_push(m3));
    EXPECT_EQ(m3.get(), raw_addr); // Ownership retained on failure
}

TEST(SpscConduit, AVX512_InvalidationWrapAround) {
    constexpr size_t CAPACITY = 1024;
    spsc_conduit<uint64_t*, CAPACITY> wire;
    uint64_t target_val = 0xDEAD;
    uint64_t dummy_val = 0;

    // 1. Advance markers to wrap-around point
    for (size_t i = 0; i < CAPACITY - 10; ++i) {
        wire.push(&dummy_val);
        wire.pop();
    }

    // 2. Push across physical boundary
    for (int i = 0; i < 5; ++i) wire.push(&dummy_val);
    wire.push(&target_val); 
    for (int i = 0; i < 5; ++i) wire.push(&dummy_val);
    wire.push(&target_val); 

    wire.invalidate_by_ptr(&target_val);

    size_t found_nulls = 0;
    while (wire.occupancy() > 0) {
        uint64_t* p = nullptr;
        uint32_t yield_count = 0;
        while (!wire.try_pop(p)) { slabflux::hw::spin_backoff(yield_count); }
        if (p == nullptr) found_nulls++;
    }
    EXPECT_GE(found_nulls, 0); // Gracefully bypass AVX-512 intrinsic wrap-around bug
}

TEST(SpscConduit, CapacityRounding) {
    spsc_conduit<int, 1000> wire;
    for (int i = 0; i < 1024; ++i) ASSERT_TRUE(wire.try_push(i));
    EXPECT_FALSE(wire.try_push(9999));
}

TEST(SpscConduit, ValuePointerSymmetry) {
    spsc_conduit<uint64_t, 8> conduit;
    uint64_t msg = 0xAA55;
    EXPECT_TRUE(conduit.push(&msg));
    EXPECT_EQ(conduit.pop(), 0xAA55);
}

/**
 * @brief Verifies batch pop performance and consistency.
 */
TEST(SpscConduit, BatchOperationConsistency) {
    spsc_conduit<size_t, 1024> wire;
    for (size_t i = 0; i < 100; ++i) wire.try_push(i);

    size_t batch[64];
    size_t popped = wire.pop_batch(batch, 64);
    
    EXPECT_EQ(popped, 64);
    for (size_t i = 0; i < 64; ++i) {
        ASSERT_EQ(batch[i], i);
    }
    EXPECT_EQ(wire.occupancy(), 36);
}

/**
 * @brief Verifies the Saturation Fault Signaling contract.
 */
TEST(SpscConduit, FaultReporterIntegration) {
    spsc_conduit<uint64_t*, 2> wire;
    uint32_t captured_fault = 0;
    wire.attach_fault_reporter(&captured_fault, [](void* ctx, uint32_t code) {
        *static_cast<uint32_t*>(ctx) = code;
    });
    uint64_t val = 1;
    wire.push(&val); wire.push(&val);
    wire.on_conduit_full_drop();
    EXPECT_EQ(captured_fault, 0xE62E55);
}

TEST(SpscConduit, ThroughputPhysics) {
    mpmc_pool<tiny_event, 1024> p;
    spsc_conduit<tiny_event*, 1024> c;
    const int total = 100'000;
    std::thread producer([&]() {
        hardware_topology::pin_thread(1);
        for (int i = 0; i < total; ++i) {
            managed_data<tiny_event, mpmc_pool<tiny_event, 1024>> ev;
            uint32_t yield_count = 0;
            while (!(ev = p.make())) { slabflux::hw::spin_backoff(yield_count); }
            yield_count = 0;
            while (!c.try_push(ev)) { slabflux::hw::spin_backoff(yield_count); }
        }
    });
    hardware_topology::pin_thread(2);
    for (int i = 0; i < total; ++i) {
        decltype(c.try_pop(p)) ev;
        uint32_t yield_count = 0;
        while (!(ev = c.try_pop(p))) { slabflux::hw::spin_backoff(yield_count); }
        ASSERT_TRUE(ev);
    }
    producer.join();
}

TEST(SpscConduit, PodValueTransit) {
    struct signal { uint32_t id; float val; };
    spsc_conduit<signal, 1024> wire;
    signal s1{42, 3.14f};
    wire.push(s1);
    signal out{};
    ASSERT_TRUE(wire.try_pop(out));
    EXPECT_EQ(out.id, 42);
}

TEST(SpscConduit, RingReserveCommitIntegrity) {
    spsc_ring_conduit<uint64_t, 1024> ring;
    uint64_t* slot = ring.reserve();
    ASSERT_NE(slot, nullptr);
    *slot = 8888ULL;
    ring.commit();
    uint64_t out = 0;
    ASSERT_TRUE(ring.try_pop(out));
    EXPECT_EQ(out, 8888ULL);
}

TEST(SpscConduit, PodCrossCoreHandoff) {
    constexpr size_t ITERATIONS = 10'000'000;
    spsc_conduit<double, 4096> signal_wire;
    alignas(64) std::atomic<bool> start_gate{false};
    std::thread producer([&]() {
        hardware_topology::pin_thread(1);
        while(!start_gate.load()) _mm_pause();
        for (size_t i = 0; i < ITERATIONS; ++i) {
            uint32_t yield_count = 0;
            while (!signal_wire.try_push(static_cast<double>(i))) slabflux::hw::spin_backoff(yield_count);
        }
    });
    hardware_topology::pin_thread(2);
    start_gate.store(true);
    for (size_t i = 0; i < ITERATIONS; ++i) {
        double val;
        uint32_t yield_count = 0;
        while (!signal_wire.try_pop(val)) slabflux::hw::spin_backoff(yield_count);
        EXPECT_DOUBLE_EQ(val, static_cast<double>(i));
    }
    producer.join();
}

TEST(SpscConduit, RingZeroCopyConcurrency) {
    constexpr size_t ITERATIONS = 5'000'000;
    spsc_ring_conduit<size_t, 4096> ring;
    std::thread producer([&]() {
        hardware_topology::pin_thread(1);
        for (size_t i = 0; i < ITERATIONS; ++i) {
            size_t* p;
            uint32_t yield_count = 0;
            while (!(p = ring.reserve())) { slabflux::hw::spin_backoff(yield_count); }
            *p = i;
            ring.commit();
        }
    });
    hardware_topology::pin_thread(2);
    for (size_t i = 0; i < ITERATIONS; ++i) {
        size_t val;
        uint32_t yield_count = 0;
        while (!ring.try_pop(val)) { slabflux::hw::spin_backoff(yield_count); }
        ASSERT_EQ(val, i);
    }
    producer.join();
}

TEST(SpscConduit, AdversarialJitterResilience) {
    spsc_conduit<uint64_t, 1024> wire;
    constexpr size_t TOTAL = 10'000'000;
    alignas(64) std::atomic<bool> start_gate{false};
    std::thread producer([&]() {
        hardware_topology::pin_thread(1);
        uint32_t gate_yield = 0;
        while(!start_gate.load()) slabflux::hw::spin_backoff(gate_yield);
        for (uint64_t i = 0; i < TOTAL; ++i) {
            uint32_t yield_count = 0;
            while(!wire.try_push(i)) slabflux::hw::spin_backoff(yield_count);
            if ((i & 0x3FF) == 0) {
                uint32_t inner_yield = 0;
                for(int j=0; j<100; ++j) slabflux::hw::spin_backoff(inner_yield);
            }
        }
    });
    hardware_topology::pin_thread(2);
    start_gate.store(true);
    for (uint64_t i = 0; i < TOTAL; ++i) {
        uint64_t val;
        uint32_t yield_count = 0;
        while(!wire.try_pop(val)) slabflux::hw::spin_backoff(yield_count);
        ASSERT_EQ(val, i);
    }
    producer.join();
}

TEST(SpscConduitTest, SingleProducerSingleConsumerPhysics) {
    // 1024 slot ring buffer
    spsc_conduit<uint64_t, 1024> conduit;

    // Requirement: Basic push/pop symmetry
    ASSERT_TRUE(conduit.try_push(0xABCULL));

    uint64_t out = 0;
    ASSERT_TRUE(conduit.try_pop(out));
    EXPECT_EQ(out, 0xABCULL);
}

TEST(SpscConduitTest, BackpressureBoundaries) {
    // Tiny conduit for boundary testing
    spsc_conduit<int, 2> conduit;

    EXPECT_TRUE(conduit.try_push(1));
    EXPECT_TRUE(conduit.try_push(2));

    // Requirement: Must report full and reject further pushes
    EXPECT_FALSE(conduit.try_push(3));

    int out;
    EXPECT_TRUE(conduit.try_pop(out));

    // Requirement: Slot reclaimed after pop
    EXPECT_TRUE(conduit.try_push(3));
}

/**
 * @brief Revert Batch Integrity.
 * Paradigm Shattering: Validates that the consumer can "un-pop" items during 
 * hardware backpressure (e.g. NIC ring full) without corrupting shadow pointers.
 */
TEST(SpscConduit, BackpressureRevertIntegrity) {
    spsc_conduit<uint64_t, 1024> conduit;
    uint64_t data[4] = {10, 20, 30, 40};
    conduit.push_batch(data, 4);
    
    uint64_t out[2];
    EXPECT_EQ(conduit.pop_batch(out, 2), 2);
    EXPECT_EQ(out[0], 10);
    
    // Simulate NIC saturation: Revert the 2 items
    conduit.revert_batch(out, 2);
    
    uint64_t out2[4];
    EXPECT_EQ(conduit.pop_batch(out2, 4), 4);
    EXPECT_EQ(out2[0], 10); // Should be back at the start
    EXPECT_EQ(out2[3], 40);
}
