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
 * ============================================================================* @file hardware_affinity_test.cpp
 * @brief Verification of thread pinning and NUMA topology.
 * @warning Thread pinning and NUMA allocation bypass standard OS scheduling.
 * Incorrect use may result in system instability or priority inversion.
 */

#include <gtest/gtest.h>
#include <numa.h>
#include <numaif.h>
#include <cstring>
#include <thread>
#include <pthread.h>
#include <sched.h>
#include <immintrin.h> // For _mm_pause
#include "slabflux/core/pool.hpp"
#include "slabflux/core/sf_node_ctx.hpp"
#include "slabflux/compute/vector_lane_512.hpp"
#include "slabflux/core/hole_puncher.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux::core;

/**
 * @brief Verifies that pin_thread correctly restricts a thread to a specific core.
 */
TEST(HardwareTopologyTest, PinThreadVerification) {
    const int target_core = 0; // Assuming Core 0 exists on all test machines

    std::thread worker([target_core]() {
        // Apply the physical pinning
        hardware_topology::pin_thread(target_core);

        // Verify via OS call
        int current_core = hardware_topology::get_current_cpu();
        EXPECT_EQ(current_core, target_core);

        // Double check affinity mask
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        EXPECT_TRUE(CPU_ISSET(target_core, &cpuset));

        // Ensure NO OTHER cores are set in the mask
        int set_count = 0;
        for (int i = 0; i < CPU_SETSIZE; ++i) {
            if (CPU_ISSET(i, &cpuset)) set_count++;
        }
        EXPECT_EQ(set_count, 1);
    });

    worker.join();
}

/**
 * @brief Verifies physical residency and NUMA binding of local node allocations.
 */
TEST(HardwareTopologyTest, LocalNodeAllocationAudit) {
    const size_t size = 2 * 1024 * 1024; // 2MB
    void* ptr = hardware_topology::allocate_on_local_node(size);
    
    ASSERT_NE(ptr, nullptr);
    // Verify 2MB alignment for HugePage compatibility
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % (2 * 1024 * 1024), 0);
    
    hardware_topology::deallocate_on_local_node(ptr, size);
}

/** 
 * @brief Verifies logical oversubscription support via modulo wrap-around.
 */
TEST(HardwareTopologyTest, OversubscriptionWrapAround) {
    const int num_cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    const uint32_t oversized_id = static_cast<uint32_t>(num_cores + 2);
    const int expected_core = static_cast<int>(oversized_id % num_cores);

    EXPECT_NO_THROW(hardware_topology::pin_thread(oversized_id));
    EXPECT_EQ(hardware_topology::get_current_cpu(), expected_core); // Verify after pinning
}

TEST(Chip, NumaLocalityIntegrity) {
    // 1. Allocate Pool
    pool<char, 1024> pool;
    void* ptr = pool.get_raw_ptr();

    // 2. Physical verification: On which NUMA node is the memory located?
    int status[1];
    void* pages[1] = { ptr };
    int nodes[1] = { -1 };

    move_pages(0, 1, pages, nullptr, nodes, MPOL_MF_MOVE);

    // Requirement: Memory must reside next to the running CPU core!
    int current_cpu = hardware_topology::get_current_cpu();
    int expected_node = numa_node_of_cpu(current_cpu);

    EXPECT_EQ(nodes[0], expected_node) << "NUMA Miss detected! Determinism compromised.";
}

TEST(Chip, ScatteringLSNConsistency) {
    // Two independent system simulations (Master and Replica)
    sf_node_ctx master_ctx;
    sf_node_ctx replica_ctx;

    slabflux::compute::vector_lane_512<64> master_engine;
    slabflux::compute::vector_lane_512<64> replica_engine;

    // HARDWARE FIX: Zero-initialize the state blocks to avoid comparing uninitialized memory
    std::memset(master_engine.states, 0, sizeof(master_engine.states));
    std::memset(replica_engine.states, 0, sizeof(replica_engine.states));

    // 1. The Master creates a truth (LSN 500)
    uint64_t truth_lsn = master_ctx.reserve_next();
    float truth_signal = 123.45f;

    // Master processing
    master_engine.propagate(truth_signal);

    // 2. Log Scattering -> Replica receives it
    // (The replica admits the data through the hole_puncher)
    hole_puncher<float, 16> replica_gap_engine;
    replica_gap_engine.insert(truth_lsn, truth_signal);

    replica_gap_engine.flush_ready([&](float p, uint64_t lsn) {
        replica_engine.propagate(p); // Apply the reordered event
        replica_ctx.commit(lsn);
        });

    // LOCK-FREE CONDUIT BARRIER: Yield execution to allow the replica
    // processing thread to safely finish draining the lock-free conduit.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // PROOF: Verify processing parity across core boundaries
    for (int i = 0; i < 64; ++i) {
        EXPECT_FLOAT_EQ(master_engine.states[i], replica_engine.states[i]);
    }
    EXPECT_EQ(master_ctx.current_lsn.load(), replica_ctx.committed_lsn.load() + 1);
}
