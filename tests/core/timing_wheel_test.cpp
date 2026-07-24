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
#include "slabflux/core/timing_wheel.hpp"
#include "slabflux/core/mpmc_pool.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux::core;

struct timeout_event : timing_node {
    uint32_t id;
};

/**
 * @brief Audits the O(1) temporal scheduler under extreme pressure.
 */
TEST(TimingWheel, TortureStress) {
    constexpr size_t CAPACITY = 4096;
    constexpr size_t ITERATIONS = 1'000'000;
    
    mpmc_pool<timeout_event, CAPACITY> pool;
    timing_wheel<mpmc_pool<timeout_event, CAPACITY>, CAPACITY> wheel(pool);

    hardware_topology::pin_thread(1);

    size_t expired_count = 0;
    auto expiry_handler = [&](timeout_event* ev) noexcept {
        expired_count++;
        pool.release(ev);
    };

    auto tick_and_process = [&]() {
        uint32_t curr = wheel.tick();
        while (curr != timing_wheel<mpmc_pool<timeout_event, CAPACITY>, CAPACITY>::END_OF_LIST) {
            auto* ev = pool.get_by_index(curr);
            // Pre-fetch next before the handler potentially releases/mutates the event
            uint32_t next = ev->timer_next;
            expiry_handler(ev);
            curr = next;
        }
    };

    for(size_t i = 0; i < ITERATIONS; ++i) {
        auto* ev = pool.make_raw();
        if (!ev) {
            // Wheel is full, tick it to clear space
            tick_and_process();
            ev = pool.make_raw();
        }
        
        if (ev) {
            ev->id = static_cast<uint32_t>(i);
            // Schedule with randomized jitter
            uint32_t delay = (i % 100) + 1;
            wheel.schedule(pool.get_index(ev), delay);
            
            // Occasional cancellation to test intrusive list integrity
            if (i % 10 == 0) {
                wheel.cancel(pool.get_index(ev));
                pool.release(ev);
            }
        }

        if (i % 10 == 0) tick_and_process();
    }

    // Final drain
    for(int i=0; i<CAPACITY; ++i) tick_and_process();
    
    EXPECT_GT(expired_count, 0);
    EXPECT_LT(expired_count, ITERATIONS);
}

/**
 * @brief Verifies Physical Architecture of timing nodes.
 */
TEST(TimingWheel, PhysicalArchitecture) {
    EXPECT_EQ(sizeof(timing_node) % 8, 0);
    
    timeout_event ev;
    // Intrusive meta must reside at the start of the object (or close to it)
    // to ensure cache-line locality during pointer-chasing wheel traversals.
    EXPECT_LT(reinterpret_cast<uintptr_t>(static_cast<timing_node*>(&ev)) - reinterpret_cast<uintptr_t>(&ev), 64);
}