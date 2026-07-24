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
 * ============================================================================* @brief SLABFLUX - Adversarial MPMC Stress Suite
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <immintrin.h> // For _mm_pause
#include "slabflux/core/mpmc_conduit.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux::core;

#include "slabflux/hw/spin_backoff.hpp"

/**
 * @brief SCENARIO 1: Producer CAS Storm
 * 16 producers fight for slots, 1 consumer drains.
 * Proves the ingress path doesn't livelock under heavy RFO (Read-For-Ownership) traffic.
 */
TEST(MpmcAdversarial, ProducerStorm) {
    constexpr size_t PRODUCERS = 16;
    constexpr size_t ITEMS_PER_PRODUCER = 50'000;
    mpmc_conduit<size_t, 1024> conduit;
    
    std::atomic<bool> start{false};
    std::vector<std::thread> producers;
    for(size_t i = 0; i < PRODUCERS; ++i) {
        producers.emplace_back([&, i]() {
            uint32_t gate_yield = 0;
            while(!start.load()) slabflux::hw::spin_backoff(gate_yield);
            for(size_t j = 0; j < ITEMS_PER_PRODUCER; ++j) {
                // Yielding blocking push to prevent OS starvation
                uint32_t yield_count = 0;
                while (!conduit.try_push(i * 1000000 + j)) {
                    slabflux::hw::spin_backoff(yield_count);
                }
            }
        });
    }

    start.store(true);
    size_t consumed = 0;
    const size_t total = PRODUCERS * ITEMS_PER_PRODUCER;
    
    uint32_t yield_count = 0;
    while(consumed < total) {
        size_t val;
        if(conduit.try_pop(val)) {
            consumed++;
            yield_count = 0;
        } else {
            slabflux::hw::spin_backoff(yield_count);
        }
    }

    for(auto& t : producers) t.join();
    EXPECT_EQ(consumed, total);
}

/**
 * @brief SCENARIO 2: Consumer Starvation Race
 * 1 producer fills, 16 consumers fight to drain.
 * Proves the egress path doesn't hang when many threads compete for the same head pointer.
 */
TEST(MpmcAdversarial, ConsumerStorm) {
    constexpr size_t CONSUMERS = 16;
    constexpr size_t TOTAL_ITEMS = 500'000;
    mpmc_conduit<size_t, 1024> conduit;
    
    std::atomic<size_t> consumed{0};
    std::vector<std::thread> consumers;
    for(size_t i = 0; i < CONSUMERS; ++i) {
        consumers.emplace_back([&]() {
            uint32_t yield_count = 0;
            while(consumed.load(std::memory_order_relaxed) < TOTAL_ITEMS) {
                size_t val;
                if(conduit.try_pop(val)) {
                    consumed.fetch_add(1, std::memory_order_relaxed);
                    yield_count = 0;
                } else {
                    slabflux::hw::spin_backoff(yield_count);
                }
            }
        });
    }

    for(size_t i = 0; i < TOTAL_ITEMS; ++i) {
        uint32_t yield_count = 0;
        while (!conduit.try_push(i)) {
            slabflux::hw::spin_backoff(yield_count);
        }
    }

    for(auto& t : consumers) t.join();
    EXPECT_EQ(consumed.load(), TOTAL_ITEMS);
}

/**
 * @brief SCENARIO 3: Saturated Wrap-Around
 * Hammers the conduit at 100% occupancy to trigger 'dif < 0' logic.
 */
TEST(MpmcAdversarial, SaturatedWrap) {
    constexpr size_t SIZE = 128;
    mpmc_conduit<size_t, SIZE> conduit;
    
    // Fill exactly to capacity
    for(size_t i = 0; i < SIZE; ++i) {
        ASSERT_TRUE(conduit.try_push(i));
    }
    
    // This should fail immediately (dif < 0)
    EXPECT_FALSE(conduit.try_push(999));

    // Start a background consumer that drains slowly
    std::atomic<bool> stop_consumer{false};
    std::thread drainer([&]() {
        uint32_t yield_count = 0;
        while(!stop_consumer.load()) {
            size_t val;
            if (conduit.try_pop(val)) {
                yield_count = 0;
            } else {
                slabflux::hw::spin_backoff(yield_count);
            }
        }
    });

    // Hammer push until we see a successful wrap
    uint32_t yield_count = 0;
    while(!conduit.try_push(1000)) {
        slabflux::hw::spin_backoff(yield_count);
    }
    
    stop_consumer.store(true);
    drainer.join();
}