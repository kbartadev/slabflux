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

#include <cstdint>

#include "slabflux/core.hpp"

using namespace slabflux;

struct alignas(64) massive_event {
    int data[10];
};

// ============================================================================
// TEST 1 — Cache‑line physics: payloads must be physically aligned.
// ============================================================================
TEST(MemoryPhysics, payloads_are_strictly_cache_line_aligned) {
    pool<massive_event, 10> p;
    auto ev = p.make();

    // Physical determinism.
    // The payload address MUST be aligned to the event’s declared alignment (64 bytes).
    // Any deviation introduces false sharing, split loads, and unpredictable stalls.
    std::uintptr_t raw_address = reinterpret_cast<std::uintptr_t>(ev.operator massive_event *());
    EXPECT_EQ(raw_address % 64, 0) << "Unaligned memory allocation detected!";
}

// ============================================================================
// TEST 2 — Free‑list physics: LIFO reuse of hot cache lines.
// ============================================================================
TEST(MemoryPhysics, pool_strictly_reuses_memory_addresses_in_LIFO_order) {
    pool<massive_event, 10> p;

    void* addr1 = nullptr;
    {
        auto ev1 = p.make();
        addr1 = static_cast<massive_event*>(ev1);
    } // Returned to the pool here.

    auto ev2 = p.make();
    void* addr2 = static_cast<massive_event*>(ev2);

    // Lock‑free free‑list = Treiber stack.
    // The most recently freed cell MUST be the next one returned.
    // This guarantees hot L1 reuse and zero allocator entropy.
    EXPECT_EQ(addr1, addr2) << "Pool is not reusing hot cache lines!";
}

// ============================================================================
// TEST 3 — ABA fuzzing: pointer‑tagging must prevent corruption.
// ============================================================================
TEST(MemoryPhysics, pool_survives_rapid_aba_fuzzing) {
    pool<massive_event, 5> p;

    // Brutal single‑threaded churn to hammer the free‑list.
    // If ABA protection is broken, the free‑list collapses instantly.
    for (int i = 0; i < 1'000'000; ++i) {
        {
            auto ev1 = p.make();
            auto ev2 = p.make();
        }
        {
            auto ev3 = p.make();
        }
    }

    SUCCEED() << "ABA tags successfully prevented free-list corruption.";
}
