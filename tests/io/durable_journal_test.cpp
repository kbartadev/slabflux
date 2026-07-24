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
 * ============================================================================* @brief SLABFLUX - Durable Journal I/O Audit
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <system_error>

#include "slabflux/core/sf_node_ctx.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/io/durable_journal.hpp"
#include "slabflux/net/wire_frame_lsn.hpp"

using namespace slabflux;

TEST(DurableJournalTest, SubmissionIntegrity) {
    const std::string path = "/tmp/slabflux_journal.test";

    // 0. Ensure a clean environment: remove leftovers from previous test runs
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }

    try {
        // Initialize the memory‑mapped (mmap) journal.
        slabflux::io::durable_journal<net::wire_frame_lsn<uint64_t>> journal(path.c_str()); // Reverted to single-argument constructor

        // 1. DO NOT create a local variable!
        // Instead, request memory directly from the disk‑backed memory map (Arena).
        auto* frame = journal.reserve_slot();
        ASSERT_NE(frame, nullptr) << "Failed to allocate memory from the Unified Arena";

        // 2. Write directly into the shared memory (mmap). Zero copy.
        frame->lsn = 100;
        frame->payload = 0xDEADBEEF;

        // 3. Notify the kernel memory manager that the data is ready for disk synchronization.
        journal.commit_slot();

        SUCCEED();
    } catch (const std::exception& e) {
        FAIL() << "Journal initialization failed: " << e.what();
    }

    // 4. Cleanup after the test to keep the CI/CD pipeline stable
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
}
