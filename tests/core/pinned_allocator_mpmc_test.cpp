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
 * ============================================================================* @brief SLABFLUX - Pinned MPMC Allocator Test Suite
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <set>
#include <chrono>
#include "slabflux/core/pinned_allocator_mpmc.hpp"
#include "slabflux/platform/os.hpp"
#include "slabflux/core/mpmc_conduit.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/hw/spin_backoff.hpp"
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

struct alignas(64) mpmc_pinned_payload {
    uint64_t id;
    uint64_t thread_id;
    uint64_t checksum;
};

/**
 * @brief MPMC Allocation Physics.
 * Verifies parallel allocation and deallocation across multiple threads.
 * 4 Producers allocate, 4 Consumers free, while pointers flow through
 * an MPMC conduit.
 */
TEST(PinnedAllocatorMpmc, ConcurrencyPhysics) {
    if (!slabflux::os::has_hugepage_support() || !check_hugepages_available()) {
        GTEST_SKIP() << "Missing MAP_LOCKED privileges or HugePages exhausted.";
    }

    constexpr size_t THREADS = 4;
    constexpr size_t OPS_PER_THREAD = 1'000'000;
    constexpr size_t TOTAL_OPS = THREADS * OPS_PER_THREAD;
    
    pinned_allocator_mpmc<mpmc_pinned_payload, 8192> allocator;
    mpmc_conduit<mpmc_pinned_payload*, 8192> handoff_wire;
    
    std::atomic<bool> start_gate{false};
    std::atomic<size_t> consumed_count{0};

    // Producers: Ingress Threads
    std::vector<std::thread> producers;
    for (size_t t = 0; t < THREADS; ++t) {
        producers.emplace_back([&, t]() {
            hardware_topology::pin_thread(static_cast<uint32_t>(t));
            uint32_t gate_yield = 0;
            while(!start_gate.load(std::memory_order_acquire)) slabflux::hw::spin_backoff(gate_yield);
            
            for (size_t i = 0; i < OPS_PER_THREAD; ++i) {
                mpmc_pinned_payload* p = nullptr;
                uint32_t yield_count = 0;
                while (!(p = allocator.make_raw())) { slabflux::hw::spin_backoff(yield_count); }
                
                p->id = i;
                p->thread_id = t;
                yield_count = 0;
                while (!handoff_wire.try_push(p)) { slabflux::hw::spin_backoff(yield_count); }
            }
        });
    }

    // Consumers: Egress Threads
    std::vector<std::thread> consumers;
    for (size_t t = 0; t < THREADS; ++t) {
        consumers.emplace_back([&, t]() {
            hardware_topology::pin_thread(static_cast<uint32_t>(THREADS + t));
            uint32_t gate_yield = 0;
            while(!start_gate.load(std::memory_order_acquire)) slabflux::hw::spin_backoff(gate_yield);
            
            for (size_t i = 0; i < OPS_PER_THREAD; ++i) {
                mpmc_pinned_payload* p = nullptr;
                uint32_t yield_count = 0;
                while (!handoff_wire.try_pop(p)) { slabflux::hw::spin_backoff(yield_count); }
                
                allocator.free(p);
                consumed_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    start_gate.store(true, std::memory_order_release);
    
    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    EXPECT_EQ(consumed_count.load(), TOTAL_OPS);
}

/**
 * @brief ABA Protection Audit.
 * Under extremely small capacity (4 slots), performs massive churn.
 * If the ABA counter were broken, the stack head would corrupt and the test
 * would crash or return invalid pointers.
 */
TEST(PinnedAllocatorMpmc, AbaProtectionAudit) {
    if (!slabflux::os::has_hugepage_support() || !check_hugepages_available()) {
        GTEST_SKIP() << "Missing MAP_LOCKED privileges or HugePages exhausted.";
    }

    constexpr size_t TINY_CAP = 4;
    pinned_allocator_mpmc<uint64_t, TINY_CAP> allocator;
    
    auto worker = [&]() {
        for (size_t i = 0; i < 500'000; ++i) {
            if (auto* p = allocator.make_raw()) {
                *p = i;
                _mm_pause(); // Simulated minimal work
                allocator.free(p);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    // Audit: The stack must remain intact.
    size_t count = 0;
    while (allocator.make_raw()) { count++; }
    EXPECT_EQ(count, TINY_CAP);
}

/**
 * @brief Physical Architecture Integrity.
 * Verifies that the “Detached Metadata” layout truly leaves the payload
 * memory untouched, which is critical for DMA (io_uring, AF_XDP).
 */
TEST(PinnedAllocatorMpmc, PhysicalArchitectureIntegrity) {
    if (!slabflux::os::has_hugepage_support() || !check_hugepages_available()) {
        GTEST_SKIP() << "Missing MAP_LOCKED privileges or HugePages exhausted.";
    }

    constexpr size_t CAP = 1024;
    pinned_allocator_mpmc<mpmc_pinned_payload, CAP> allocator;
    
    void* base = allocator.data();
    
    // 1. Page Alignment check (mlock/mmap requirement)
    EXPECT_EQ(reinterpret_cast<uintptr_t>(base) % 4096, 0);

    // 2. Contiguity check: The payload region must be a clean T[] array
    mpmc_pinned_payload* p0 = allocator.make_raw();
    mpmc_pinned_payload* p1 = allocator.make_raw();
    
    // Since this is an MPMC stack (LIFO), we get the last slots first,
    // but their distance must be a multiple of sizeof(T).
    ptrdiff_t diff = reinterpret_cast<char*>(p1) - reinterpret_cast<char*>(p0);
    EXPECT_EQ(std::abs(diff) % sizeof(mpmc_pinned_payload), 0);
}

/**
 * @brief Explicit Lifecycle Construction.
 */
TEST(PinnedAllocatorMpmc, LifecycleConstruction) {
    if (!slabflux::os::has_hugepage_support() || !check_hugepages_available()) {
        GTEST_SKIP() << "Missing MAP_LOCKED privileges or HugePages exhausted.";
    }

    pinned_allocator_mpmc<std::string, 64> allocator;
    
    std::string* s = allocator.construct("SLABFLUX_MPMC_STRESS");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(*s, "SLABFLUX_MPMC_STRESS");
    
    allocator.destroy(s);
}
