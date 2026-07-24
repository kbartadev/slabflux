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
 * ============================================================================* @brief SLABFLUX - Pinned SPSC Allocator Test Suite
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <immintrin.h>
#include <chrono>
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/platform/os.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include <sys/mman.h>

using namespace slabflux::core;

inline bool check_hugepages_available() {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE | MAP_LOCKED;
    void* test_mmap = ::mmap(nullptr, 2 * 1024 * 1024, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (test_mmap == MAP_FAILED) {
        std::cerr << "\n[!] SYSTEM PERMISSION DENIED (EPERM / ENOMEM) [!]\n"
                  << "    HugePages are allocated, but the OS denied the MAP_LOCKED request.\n"
                  << "    -> SOLUTION: Run tests with 'sudo' or set 'ulimit -l unlimited'.\n\n";
        return false;
    }
    ::munmap(test_mmap, 2 * 1024 * 1024);
    return true;
}

struct alignas(64) spsc_pinned_payload {
    uint64_t data[8];
};

#include "slabflux/hw/spin_backoff.hpp"

/**
 * @brief SPSC Allocation Physics.
 * Validates the pure 1:1 ownership handoff between two isolated hardware threads.
 * One thread (Ingress) allocates from the SPSC freelist, another (Compute) returns memory.
 * 
 * High-Performance Mechanics:
 * - Shadow Pointer Stash: Allocator avoids atomic loads until local knowledge is zero.
 * - Cache-Line Isolation: Producer and Consumer states reside on separate L1 cache slices.
 */
TEST(PinnedAllocatorSpsc, HandoffPhysics) {
    if (!slabflux::os::has_hugepage_support() || !check_hugepages_available()) {
        GTEST_SKIP() << "Missing MAP_LOCKED privileges or HugePages exhausted.";
    }

    constexpr size_t OPS = 5'000'000;
    // 4096 capacity allows for deep pipeline bursting
    pinned_allocator_spsc<spsc_pinned_payload, 4096> allocator;
    
    // Wire: Used to pass allocated pointers from Producer to Consumer
    spsc_conduit<spsc_pinned_payload*, 4096> handoff_wire;
    
    alignas(64) std::atomic<bool> start_gate{false};
    std::atomic<size_t> received_count{0};

    // Producer: Ingress Thread (The Allocator)
    std::thread ingress_thread([&]() {
        hardware_topology::pin_thread(1);
        uint32_t gate_yield = 0;
        while(!start_gate.load(std::memory_order_acquire)) slabflux::hw::spin_backoff(gate_yield);
        
        for (size_t i = 0; i < OPS; ++i) {
            spsc_pinned_payload* p = nullptr;
            // Shadow Pointer Path: 0.6ns acquisition while local stash is warm.
            uint32_t yield_count = 0;
            while (!(p = allocator.make_raw())) { slabflux::hw::spin_backoff(yield_count); }
            
            p->data[0] = i;
            // Transfer ownership to the Compute thread
            yield_count = 0;
            while (!handoff_wire.try_push(p)) { slabflux::hw::spin_backoff(yield_count); }
        }
    });

    // Consumer: Compute Thread (The Freer)
    hardware_topology::pin_thread(2);
    start_gate.store(true, std::memory_order_release);

    for (size_t i = 0; i < OPS; ++i) {
        spsc_pinned_payload* p = nullptr;
        uint32_t yield_count = 0;
        while (!handoff_wire.try_pop(p)) { slabflux::hw::spin_backoff(yield_count); }
        
        if (SL_EXPECT_FALSE(p->data[0] != i)) {
            std::abort(); // Determinism breach
        }
        
        // Return memory to the SPSC free-pool
        allocator.free(p);
        received_count++;
    }
    
    ingress_thread.join();
    EXPECT_EQ(received_count.load(), OPS);
}

/**
 * @brief Shadow Consistency and Recovery.
 * Verifies that the allocator correctly triggers a high-watermark pull 
 * from the consumer thread when the local shadow stash is exhausted.
 */
TEST(PinnedAllocatorSpsc, ShadowConsistencyAudit) {
    if (!slabflux::os::has_hugepage_support() || !check_hugepages_available()) {
        GTEST_SKIP() << "Missing MAP_LOCKED privileges or HugePages exhausted.";
    }

    constexpr size_t CAP = 64;
    pinned_allocator_spsc<size_t, CAP> allocator;
    
    std::vector<size_t*> ptrs;
    for (size_t i = 0; i < CAP; ++i) {
        auto* p = allocator.make_raw();
        ASSERT_NE(p, nullptr);
        ptrs.push_back(p);
    }
    
    // 1. Initial Exhaustion: Shadow pointer must be equal to head.
    EXPECT_TRUE(allocator.is_empty());
    EXPECT_EQ(allocator.make_raw(), nullptr);

    // 2. Side-channel return: Simulate a release from another thread
    allocator.free(ptrs.back());
    ptrs.pop_back();
    
    // 3. High-Watermark Pull: make_raw() should now refresh from shared state
    size_t* recovered = allocator.make_raw();
    EXPECT_NE(recovered, nullptr);
    EXPECT_TRUE(allocator.is_empty());
}

/**
 * @brief Physical Architecture Integrity.
 */
TEST(PinnedAllocatorSpsc, PhysicalArchitectureIntegrity) {
    if (!slabflux::os::has_hugepage_support() || !check_hugepages_available()) {
        GTEST_SKIP() << "Missing MAP_LOCKED privileges or HugePages exhausted.";
    }

    constexpr size_t CAP = 1024;
    pinned_allocator_spsc<spsc_pinned_payload, CAP> allocator;
    
    // Base Memory Alignment: Critical for DMA engine compatibility
    void* base = allocator.data();
    EXPECT_EQ(reinterpret_cast<uintptr_t>(base) % 64, 0);
    
    // Stride validation: Ensures O(1) index-to-pointer resolution is bit-perfect
    auto* p0 = allocator.get_ptr(0);
    auto* p1 = allocator.get_ptr(1);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p1) - reinterpret_cast<uintptr_t>(p0), sizeof(spsc_pinned_payload));
    
    // Memory footprint audit
    EXPECT_GE(allocator.size_bytes(), CAP * sizeof(spsc_pinned_payload));
}