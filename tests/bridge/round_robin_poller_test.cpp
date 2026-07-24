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
 */

#include <gtest/gtest.h>
#include <chrono>
#include "slabflux/bridge/round_robin_poller.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/mpmc_pool.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/hw/spin_backoff.hpp"

using namespace slabflux::core;
using namespace slabflux::bridge;
struct load_event { int source; };

TEST(RoundRobinPoller, FairShareExtraction) {
    spsc_conduit<load_event*, 1024> ca, cb;
    round_robin_poller<load_event, 2> poller;
    poller.bind_track(0, ca); poller.bind_track(1, cb);
    load_event v1{0xA}, v2{0xB};
    ca.push(&v1); cb.push(&v2);
    EXPECT_EQ(poller.poll()->source, 0xA);
    EXPECT_EQ(poller.poll()->source, 0xB);
    EXPECT_EQ(poller.poll(), nullptr);
}

TEST(RoundRobinPoller, StarvationPrevention) {
    mpmc_pool<load_event, 100> p;
    spsc_conduit<load_event*, 10> t1, t2;
    round_robin_poller<load_event, 2> poller;
    poller.bind_track(0, t1); poller.bind_track(1, t2);
    t1.push(p.make(1).release());
    t1.push(p.make(1).release());
    t2.push(p.make(2).release());
    EXPECT_EQ(poller.poll()->source, 1);
    EXPECT_EQ(poller.poll()->source, 2);
    EXPECT_EQ(poller.poll()->source, 1);
}

/**
 * @brief Branch Predictor Shielding Audit.
 */
TEST(RoundRobinPoller, BranchNeutrality) {
    spsc_conduit<load_event*, 1024> conduits[16];
    round_robin_poller<load_event, 16> poller;
    for(int i=0; i<16; ++i) poller.bind_track(i, conduits[i]);

    load_event evs[16];
    // Interleave empty and full tracks to stress BTB
    for(int i=0; i<16; i+=2) conduits[i].push(&evs[i]);

    uint64_t start = __rdtsc();
    for(int i=0; i<8; ++i) {
        auto* out = poller.poll();
        ASSERT_NE(out, nullptr);
    }
    uint64_t delta = __rdtsc() - start;

    EXPECT_LT(delta / 8, 1000) << "Branch predictor decay detected.";
}

/**
 * @brief High-Velocity Fan-In Stress.
 */
TEST(RoundRobinPoller, HighVelocityFanIn) {
    constexpr size_t PRODUCERS = 4;
    constexpr size_t ITEMS_PER_PRODUCER = 5'000'000;
    spsc_conduit<load_event*, 4096> conduits[PRODUCERS];
    round_robin_poller<load_event, PRODUCERS> poller;
    for(size_t i=0; i<PRODUCERS; ++i) poller.bind_track(i, conduits[i]);

    alignas(64) std::atomic<bool> start_gate{false};
    std::vector<load_event> storage(PRODUCERS * ITEMS_PER_PRODUCER);
    std::vector<std::thread> producers;

    for(size_t i=0; i<PRODUCERS; ++i) {
        producers.emplace_back([&, i]() {
            hardware_topology::pin_thread(static_cast<int>(i + 1));
            uint32_t yield_count = 0;
            while(!start_gate.load()) slabflux::hw::spin_backoff(yield_count);
            yield_count = 0;
            for(size_t j=0; j<ITEMS_PER_PRODUCER; ++j) {
                while(!conduits[i].try_push(&storage[i * ITEMS_PER_PRODUCER + j])) slabflux::hw::spin_backoff(yield_count);
                yield_count = 0;
            }
        });
    }

    hardware_topology::pin_thread(0);
    start_gate.store(true);

    size_t received = 0;
    const size_t total_expected = PRODUCERS * ITEMS_PER_PRODUCER;
    uint32_t yield_count = 0;
    while(received < total_expected) {
        if(poller.poll()) { received++; yield_count = 0; }
        else slabflux::hw::spin_backoff(yield_count);
    }

    for(auto& t : producers) t.join();
    EXPECT_EQ(received, total_expected);
}
