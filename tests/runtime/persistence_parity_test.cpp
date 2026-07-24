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
 * @file persistence_parity_test.cpp
 */
 
#include <gtest/gtest.h>
#include <thread> // Required for std::thread
#include <filesystem>
#include <thread>
#include <x86intrin.h>
#include "slabflux/core.hpp"
#include "slabflux/storage/durable_storage.hpp"
#include "slabflux/storage/durable_sink.hpp"
#include "slabflux/hw/spin_backoff.hpp"

using namespace slabflux;

/**
 * @brief Round-trip persistence test.
 * Verifies that a stream of POD events remains bit-identical after
 * being written to disk and read back.
 */
TEST(PersistenceTest, SinkSourceRoundTripParity) {
    const std::string log_path = "test_events.alog";
    struct alignas(64) trade_event {
        uint64_t lsn;
        double price;
        uint32_t qty;
    };

    // Start with a clean slate
    std::filesystem::remove(log_path);

    // ========================================================================
    // 1. RECORD PHASE (ZERO-COPY)
    // No stack allocation. All data is born directly inside the NVMe mmap.
    // ========================================================================
    { // Corrected template arguments for durable_sink
        storage::durable_sink<trade_event, slabflux::io::durable_journal<trade_event>> sink(log_path.c_str());

        for (uint64_t i = 0; i < 100; ++i) {
            // 1. Reserve memory directly inside the memory-mapped region
            trade_event* ev = sink.reserve_slot();
            ASSERT_NE(ev, nullptr) << "Fatal: Arena memory exhaustion";

            // 2. Write the data (this is already physically in the page cache)
            ev->lsn = i;
            ev->price = 100.0 + i;
            ev->qty = 10 + static_cast<uint32_t>(i);

            // 3. Notify the logger that the memory is ready for flushing
            sink.commit();
        }
        // At the end of the scope, the sink destructor runs and guarantees disk flush.
    }

    // ========================================================================
    // 2. REPLAY & VERIFY PHASE (ZERO-COPY)
    // ========================================================================
    {
        // ARCHITECTURAL NOTE:
        // In a full zero-copy world, `core::pool` becomes unnecessary on the Source side,
        // because `durable_source` *is* the memory map (mmap). It should push the raw
        // memory-mapped file addresses directly into the `replay_bus`.

        core::pool<trade_event, 128> pool;
        conduit<trade_event*, 128> replay_bus;

        storage::durable_source<decltype(pool), decltype(replay_bus), trade_event>
        source(pool, replay_bus, log_path.c_str());

        std::thread replay_thread([&]() { source.run_replay(); });

        for (uint64_t i = 0; i < 100; ++i) {
            auto ev = replay_bus.pop(pool);

            uint32_t yield_count = 0;
            while (!ev) {
                slabflux::hw::spin_backoff(yield_count);
                ev = replay_bus.pop(pool);
            }

            // Bit-level comparison
            EXPECT_EQ(ev->lsn, i);
            EXPECT_DOUBLE_EQ(ev->price, 100.0 + i);
            EXPECT_EQ(ev->qty, 10 + i);
        }

        replay_thread.join();
    }
}
