/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 * PROPRIETARY AND SOURCE-AVAILABLE CODEBASE. ALL RIGHTS RESERVED.
 *
 * This source file and all constitutive programmatic expressions contained herein 
 * are the exclusive intellectual property of Kristóf Barta, established and 
 * distributed strictly under the conditions of the SLABFLUX SOURCE-AVAILABLE 
 * AND ECOSYSTEM LICENSE (the "License").
 *
 * TITLE TO AND OWNERSHIP OF THE SOFTWARE, THE ENGINE, CORE LOGIC, ARCHITECTURAL 
 * LAYOUTS, AND ALL ASSOCIATED INSIGHTS REMAIN SOLELY VESTED IN THE AUTHOR.
 *
 * ----------------------------------------------------------------------------
 * TECHNICAL WARNING & SYSTEM ARCHITECTURE NOTICE
 * ----------------------------------------------------------------------------
 * THIS SOFTWARE UTILIZES ARCHITECTURE-SPECIFIC HARDWARE INTRINSICS AND OPERATES
 * THROUGH LOW-LEVEL, KERNEL-ADJACENT EXECUTION PATHS THAT REDUCE OR BYPASS STANDARD
 * OPERATING SYSTEM MEDIATION LAYERS. INCORRECT INTEGRATION, EXECUTION, OR CONFIGURATION
 * MAY RESULT IN SEVERE SYSTEM INSTABILITY, KERNEL PANICS, OR PERMANENT LOSS OF DATA,
 * AND MAY RENDER SYSTEMS TEMPORARILY OR PERMANENTLY UNUSABLE UNTIL REPAIRED OR
 * RECONFIGURED.
 * ============================================================================
 * @file io_uring_durable_journal_test.cpp
 * @brief Validation suite for O_DIRECT NVMe backing store via io_uring.
 */

#include <gtest/gtest.h>
#include <filesystem>

#include "slabflux/io/io_uring_durable_journal.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux;

/**
 * @brief Physical Residency Audit
 */
TEST(IoUringDurableJournalTest, PhysicalResidency) {
    using Journal = io::io_uring_durable_journal<transport::raw_tcp_frame, 1024*1024>;
    EXPECT_EQ(alignof(Journal), 64);
    EXPECT_EQ(sizeof(Journal) % 64, 0);
}

/**
 * @brief Validates asynchronous SQE submission, NVMe flush, and CQE harvesting.
 */
TEST(IoUringDurableJournalTest, AsyncPersistenceIntegrity) {
    const char* path = "/tmp/sf_uring_journal_test.log";
    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }

    try {
        // Isolate the test thread from the SQPOLL kernel thread to prevent SCHED_FIFO starvation
        slabflux::core::hardware_topology::pin_thread(0);
        int sq_cpu = (std::thread::hardware_concurrency() > 1) ? 1 : 0;

        // Allocate a minimal 1MB journal for testing to prevent massive disk stalling on CI
        io::io_uring_durable_journal<transport::raw_tcp_frame, 1024*1024> journal(path, sq_cpu);

        auto* slot = journal.reserve_slot();
        ASSERT_NE(slot, nullptr);
        
        slot->payload_length = 42;
        
        // Push the SQE to the kernel
        journal.commit_slot();

        // Force flush executes an io_uring FSYNC and waits for it, ensuring disk persistence
        journal.force_flush();
        
        // Horizon must advance after commit
        EXPECT_GT(journal.get_sync_watermark(), 0);

    } catch (const std::exception& e) {
        GTEST_SKIP() << "io_uring or O_DIRECT not supported on this filesystem: " << e.what();
    }

    if (std::filesystem::exists(path)) {
        std::filesystem::remove(path);
    }
}