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
 * ============================================================================* @file memory_pinning_failure_test.cpp
 * @brief Verifies graceful degradation when mlock is denied by the OS.
 */

#include <gtest/gtest.h>

#ifndef _WIN32

#include <sys/resource.h>
#include <sys/mman.h>
#include <unistd.h>

#include "slabflux/core/pinned_allocator_spsc.hpp"

TEST(MemoryPinning, SpscAllocatorSurvivesMemlockDenial) {
    const rlimit previous = [] {
        rlimit current{};
        getrlimit(RLIMIT_MEMLOCK, &current);
        return current;
    }();

    rlimit deny{};
    deny.rlim_cur = 0;
    deny.rlim_max = 0;
    ASSERT_EQ(setrlimit(RLIMIT_MEMLOCK, &deny), 0);

    slabflux::core::pinned_allocator_spsc<int, 64> allocator;
    int* slot = allocator.make_raw();
    EXPECT_NE(slot, nullptr);
    allocator.free(slot);

    // Best-effort restore; may fail without CAP_SYS_RESOURCE.
    (void)setrlimit(RLIMIT_MEMLOCK, &previous);
}

TEST(MemoryPinning, MlockOnAnonymousMappingFailsCleanlyUnderZeroLimit) {
    if (geteuid() == 0) {
        GTEST_SKIP() << "Test bypassed: root privileges override RLIMIT_MEMLOCK via CAP_IPC_LOCK.";
    }
    void* mem = ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(mem, MAP_FAILED);

    rlimit deny{};
    deny.rlim_cur = 0;
    deny.rlim_max = 0;
    ASSERT_EQ(setrlimit(RLIMIT_MEMLOCK, &deny), 0);

    EXPECT_NE(::mlock(mem, 4096), 0);

    munmap(mem, 4096);
}

#endif  // !_WIN32
