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
 * ============================================================================*/
#include <gtest/gtest.h>
#include "slabflux/sys/clock_node.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include "slabflux/sys/clock_node.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/hw/spin_backoff.hpp"

using namespace slabflux;
using namespace slabflux::io;
using namespace slabflux::sys;

struct MockAllocator {
    tick_event pool[16];
    size_t head = 0;

    tick_event* make_raw() {
        if (head >= 16) return nullptr;
        return &pool[head++];
    }

    // Interface: Mock the reclamation path required by the Clock Node
    void release(tick_event*) noexcept {}
    void free(tick_event* ptr) noexcept { release(ptr); }
};

struct MockBus {
    std::vector<tick_event*> events;
    bool push(tick_event* ev) {
        events.push_back(ev);
        return true;
    }
};

TEST(ClockNodeTest, Calibration) {
    MockAllocator alloc;
    MockBus bus;

    // This will perform calibration
    clock_node<MockAllocator, MockBus> node(alloc, bus, 1000);
    SUCCEED();
}

TEST(ClockNodeTest, RunAndStop) {
    MockAllocator alloc;
    MockBus bus;
    clock_node<MockAllocator, MockBus> node(alloc, bus, 1000000); // 1ms resolution

    std::thread t([&]() {
        node.run();
    });

    // Wait for some ticks to be generated
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    node.stop();
    t.join();

    EXPECT_GT(bus.events.size(), 0);
    if (bus.events.size() > 1) {
        EXPECT_GE(bus.events[1]->timestamp_ns, bus.events[0]->timestamp_ns + 1000000);
    }
}

TEST(ClockNodeTest, TemporalPumpingIntegrity) {
    using TimeAlloc = core::pinned_allocator_spsc<sys::tick_event, 1024>;
    using TimeBus   = core::spsc_conduit<sys::tick_event*, 1024>;

    TimeAlloc pool;
    TimeBus bus;

    // Resolution: 10ms ticks to avoid flooding during test
    io::clock_node<TimeAlloc, TimeBus> clock(pool, bus, 10'000'000);

    std::thread clock_thread([&]() {
        clock.run();
    });

    // Collect 3 ticks from the temporal motor
    size_t count = 0;
    uint32_t yield_count = 0;
    while(count < 3) {
        sys::tick_event* tick = nullptr;
        if (bus.try_pop(tick) && tick) {
            EXPECT_GE(tick->delta_ns, 10'000'000);
            pool.free(tick);
            count++;
            yield_count = 0;
        } else {
            slabflux::hw::spin_backoff(yield_count);
        }
    }

    clock.stop();
    if (clock_thread.joinable()) clock_thread.join();
    SUCCEED();
}
