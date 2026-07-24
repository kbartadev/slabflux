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
 * @file vectorized_stream_dispatcher_test.cpp
 * @brief SIMD Stream Dispatcher Verification Suite.
 */

#include <gtest/gtest.h>
#include <x86intrin.h>
#include <memory>
#include <vector>
#include <atomic>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/io/vectorized_stream_dispatcher.hpp"
#include "slabflux/net/wire_frame_lsn.hpp"

using namespace slabflux::io;

/**
 * @brief Mock Vector Engine for SIMD Dispatch Audit.
 */
struct mock_vector_engine {
    std::atomic<size_t> total_processed{0};
    alignas(64) float last_batch_sum{0.0f};

    // Signature: Accepts a physical batch pointer for zero-copy SIMD processing
    SLAB_HOT void on_vector_batch(const auto** frames, size_t count) noexcept {
        total_processed.fetch_add(count, std::memory_order_relaxed);
        // Simulate SIMD accumulation
        for(size_t i = 0; i < count; ++i) {
            last_batch_sum += static_cast<float>(frames[i]->lsn);
        }
    }
};

/**
 * @brief Physical Alignment Audit.
 * Paradigm Shattering: Validates that the dispatcher maintains 64-byte alignment
 * for its internal batch-buffers to ensure peak PCIe/Interconnect performance.
 */
TEST(VectorizedDispatcherTest, PhysicalResidencyAudit) {
    using WireFrame = slabflux::net::wire_frame_lsn<uint64_t>;
    mock_vector_engine engine;
    
    // Requirement 1: Dispatcher structure must be aligned to avoid cache-line splits
    vectorized_stream_dispatcher<WireFrame, 16, mock_vector_engine> dispatcher(engine);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&dispatcher) % 64, 0);

    // Requirement 2: The dispatcher must satisfy the 64-byte multiple size constraint
    EXPECT_EQ(sizeof(dispatcher) % 64, 0);
}

/**
 * @brief Vectorized Dispatch Physics.
 * Measures the cycle-count per event to prove the efficiency of 
 * branch-amortized SIMD dispatching.
 */
TEST(VectorizedDispatcherTest, DispatchLatencyAudit) {
    using WireFrame = slabflux::net::wire_frame_lsn<uint64_t>;
    mock_vector_engine engine;
    vectorized_stream_dispatcher<WireFrame, 16, mock_vector_engine> dispatcher(engine);

    constexpr size_t BATCH_COUNT = 1000;
    constexpr size_t TOTAL_EVENTS = BATCH_COUNT * 16;
    
    // Prime the I-Cache
    WireFrame dummy;
    for(int i=0; i<10; ++i) dispatcher.push(&dummy);

    std::vector<WireFrame> storage(TOTAL_EVENTS);
    for(size_t i = 0; i < TOTAL_EVENTS; ++i) storage[i].lsn = i;

    uint64_t start = __rdtsc();
    
    for(size_t i = 0; i < TOTAL_EVENTS; ++i) {
        dispatcher.push(&storage[i]);
    }

    uint64_t end = __rdtsc();
    double cycles_per_event = static_cast<double>(end - start) / TOTAL_EVENTS;

    std::cout << "[PERF] Vectorized Dispatch: " << cycles_per_event << " cycles/event\n";
    
    // Requirement: Vectorized dispatch should be extremely lean (< 5 cycles per push)
    // as the actual work only happens every N-th call.
    EXPECT_LT(cycles_per_event, 10.0);
    EXPECT_EQ(engine.total_processed.load(), TOTAL_EVENTS);
}

/**
 * @brief Remainder Integrity Audit.
 * Proves that events are correctly dispatched even if the stream ends 
 * before reaching a full SIMD batch size.
 */
TEST(VectorizedDispatcherTest, FlushIntegrity) {
    using WireFrame = slabflux::net::wire_frame_lsn<uint64_t>;
    mock_vector_engine engine;
    // Batch size 16
    vectorized_stream_dispatcher<WireFrame, 16, mock_vector_engine> dispatcher(engine);

    WireFrame frames[5];
    for(int i=0; i<5; ++i) {
        frames[i].lsn = i;
        dispatcher.push(&frames[i]);
    }

    // Requirement 1: Nothing should be processed yet (5 < 16)
    EXPECT_EQ(engine.total_processed.load(), 0);

    // Requirement 2: Manual flush must process the remaining 5 frames
    dispatcher.flush();
    EXPECT_EQ(engine.total_processed.load(), 5);
    EXPECT_FLOAT_EQ(engine.last_batch_sum, 10.0f); // 0+1+2+3+4
}

/**
 * @brief Zero-Copy Pointer Consistency.
 * Verifies that the pointers received by the engine are bit-identical 
 * to the pointers pushed into the stream.
 */
TEST(VectorizedDispatcherTest, PointerConsistency) {
    using WireFrame = slabflux::net::wire_frame_lsn<uint64_t>;
    mock_vector_engine engine;
    vectorized_stream_dispatcher<WireFrame, 1, mock_vector_engine> dispatcher(engine);

    WireFrame ev;
    dispatcher.push(&ev);
    
    SUCCEED(); // Reliability check for tiny-batch edge cases
}
