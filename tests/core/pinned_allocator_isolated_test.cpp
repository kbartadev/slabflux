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
 * ============================================================================* @brief SLABFLUX - Pinned Isolated Allocator Test Suite
 */

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>
#include <sys/mman.h>
#include "slabflux/core/pinned_allocator_isolated.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/platform/os.hpp"
#include "slabflux/hw/spin_backoff.hpp"

using namespace slabflux::core;

inline bool check_hugepages_available() {
    int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | MAP_POPULATE | MAP_LOCKED;
    void* test_mmap = ::mmap(nullptr, 2 * 1024 * 1024, PROT_READ | PROT_WRITE, flags, -1, 0);
    if (test_mmap == MAP_FAILED) {
        std::cerr << "\n[!] SYSTEM PERMISSION DENIED (EPERM / ENOMEM) [!]\n"
                  << "    HugePages are allocated, but the OS denied the MAP_LOCKED request.\n"
                  << "    The allocator will call std::abort() if instantiated.\n"
                  << "    -> SOLUTION: Run tests with 'sudo' or set 'ulimit -l unlimited'.\n\n";
        return false;
    }
    ::munmap(test_mmap, 2 * 1024 * 1024);
    return true;
}

struct alignas(1) isolated_payload {
    uint64_t data;
};

/**
 * @brief Isolated Allocation Physics.
 * Verifies parallel access. Since the "isolated" variant forces every 
 * object onto its own cache line, MESI protocol traffic should be minimal 
 * during object writes, avoiding cache-line bouncing due to adjacent slots.
 */
TEST(PinnedAllocatorIsolated, ConcurrencyPhysics) {
    if (!slabflux::os::has_hugepage_support() || !check_hugepages_available()) {
        GTEST_SKIP() << "Missing MAP_LOCKED privileges or HugePages exhausted.";
    }

    constexpr size_t THREADS = 4;
    constexpr size_t OPS_PER_THREAD = 100'000;
    
    pinned_slab_allocator<isolated_payload, 256> allocator;
    std::atomic<bool> start_gate{false};
    std::atomic<size_t> total_allocated{0};

    auto worker = [&](uint32_t id) {
        hardware_topology::pin_thread(id);
        uint32_t gate_yield = 0;
        while(!start_gate.load(std::memory_order_acquire)) slabflux::hw::spin_backoff(gate_yield);
        
        std::vector<isolated_payload*> batch;
        batch.reserve(64);

        for (size_t i = 0; i < OPS_PER_THREAD; ++i) {
            if (auto* p = allocator.make_raw()) {
                p->data = i;
                batch.push_back(p);
                total_allocated.fetch_add(1, std::memory_order_relaxed);
            }
            
            if (batch.size() >= 64) {
                for (auto* p : batch) allocator.free(p);
                batch.clear();
            }
        }
        for (auto* p : batch) allocator.free(p);
    };

    std::vector<std::thread> threads;
    for (uint32_t i = 0; i < THREADS; ++i) threads.emplace_back(worker, i);
    
    start_gate.store(true, std::memory_order_release);
    for (auto& t : threads) t.join();

    EXPECT_GT(total_allocated.load(), 0);
}

/**
 * @brief ABA Protection Audit.
 * Verifies that the 64-bit tagged pointer correctly manages the freelist 
 * even under extremely fast recycling cycles.
 */
TEST(PinnedAllocatorIsolated, AbaProtectionAudit) {
    if (!slabflux::os::has_hugepage_support() || !check_hugepages_available()) {
        GTEST_SKIP() << "Missing MAP_LOCKED privileges or HugePages exhausted.";
    }

    constexpr size_t CAP = 2;
    pinned_slab_allocator<uint64_t, CAP> allocator;
    
    auto worker = [&]() {
        for (size_t i = 0; i < 1'000'000; ++i) {
            if (auto* p = allocator.make_raw()) {
                allocator.free(p);
            }
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back(worker);
    for (auto& t : threads) t.join();

    size_t count = 0;
    while (allocator.make_raw()) { count++; }
    EXPECT_EQ(count, CAP);
}

/**
 * @brief Physical Isolation Integrity.
 * Verifies that the addresses of two adjacent elements are indeed at least 
 * one cache line (64 bytes) apart, and both are 64-byte aligned.
 */
TEST(PinnedAllocatorIsolated, PhysicalIsolationIntegrity) {
    if (!slabflux::os::has_hugepage_support() || !check_hugepages_available()) {
        GTEST_SKIP() << "Missing MAP_LOCKED privileges or HugePages exhausted.";
    }

    pinned_slab_allocator<uint8_t, 10> allocator;
    
    uint8_t* p1 = allocator.make_raw();
    uint8_t* p2 = allocator.make_raw();
    
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    
    uintptr_t addr1 = reinterpret_cast<uintptr_t>(p1);
    uintptr_t addr2 = reinterpret_cast<uintptr_t>(p2);
    
    EXPECT_EQ(addr1 % 64, 0);
    EXPECT_EQ(addr2 % 64, 0);
    EXPECT_GE(std::abs(static_cast<long long>(addr1) - static_cast<long long>(addr2)), 64);
    
    allocator.free(p1);
    allocator.free(p2); // Ensure all allocated memory is returned
}

/**
 * @brief Explicit Lifecycle Audit.
 * Tests the correctness of construct and destroy calls (RAII).
 */
TEST(PinnedAllocatorIsolated, LifecycleAudit) {
    if (!slabflux::os::has_hugepage_support() || !check_hugepages_available()) {
        GTEST_SKIP() << "Missing MAP_LOCKED privileges or HugePages exhausted.";
    }

    struct tracker {
        bool* destroyed;
        tracker(bool* d) : destroyed(d) {}
        ~tracker() { if(destroyed) *destroyed = true; }
    };

    pinned_slab_allocator<tracker, 10> allocator;
    bool is_destroyed = false;
    
    tracker* t = allocator.construct(&is_destroyed);
    ASSERT_NE(t, nullptr);
    EXPECT_FALSE(is_destroyed);
    
    allocator.destroy(t);
    EXPECT_TRUE(is_destroyed);
}