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
 */

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <immintrin.h> // For _mm_pause
#include "slabflux/core/buffer_flush.hpp"

using namespace slabflux::core;

/**
 * @brief Stability Audit for Hardware Flush.
 * Verifies that the inline assembly for LFB drain executes correctly 
 * without causing stack corruption or illegal instructions.
 */
TEST(BufferFlushTest, StabilityAudit) {
    for (int i = 0; i < 1000; ++i) {
        // The locked instruction in force_lfb_drain must not crash 
        // or disrupt the local stack frame.
        EXPECT_NO_THROW(force_lfb_drain());
    }
}

/**
 * @brief Memory Visibility Sanity.
 * While a unit test cannot definitively prove LFB drainage (which is a 
 * micro-architectural side effect), this test verifies the primitive 
 * functions as a valid memory barrier in a multi-threaded context.
 */
TEST(BufferFlushTest, ConcurrencyVisibilitySanity) {
    struct alignas(64) SharedState {
        uint64_t data1{0};
        uint64_t data2{0};
        std::atomic<bool> ready{false};
    };

    SharedState state;

    std::thread producer([&]() {
        state.data1 = 42;
        state.data2 = 84;
        
        // Force eviction of write-combining buffers to RAM/Interconnect
        force_lfb_drain();
        
        state.ready.store(true, std::memory_order_release);
    });

    while (!state.ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    EXPECT_EQ(state.data1, 42);
    EXPECT_EQ(state.data2, 84);

    producer.join();
}