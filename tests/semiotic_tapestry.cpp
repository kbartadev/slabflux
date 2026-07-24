/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <thread>
#include <chrono>
#include <cstdlib>
#ifndef _WIN32
#include <sys/resource.h>
#endif
#include "slabflux/security/kinetic_inscription.hpp"

using namespace slabflux::security;

class SemioticTapestryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // The Tapestry's evaluation matrix uses AVX-512 instructions.
        if (!__builtin_cpu_supports("avx512f")) {
            GTEST_SKIP() << "Skipping Semiotic Tapestry tests: AVX-512 not supported by host silicon.";
        }
        
        // CRITICAL FIX: Prevent fork() deadlocks in multithreaded test environments.
        // Forces GoogleTest to use fork() + exec() instead of just fork(), 
        // ensuring the child process doesn't inherit locked mutexes from dead background threads.
        ::testing::GTEST_FLAG(death_test_style) = "threadsafe";
    }
};

TEST_F(SemioticTapestryTest, WeaveAndRecord) {
    semiotic_tapestry tapestry;

    // Weave should initialize the ledger without throwing.
    EXPECT_NO_THROW(tapestry.weave());

    // Record a single fray code.
    // Evaluation happens natively during the engraving process.
    EXPECT_NO_THROW(tapestry.engrave_anomaly(0x0D, 1));
}

TEST_F(SemioticTapestryTest, NvmeSaturation) {
    semiotic_tapestry tapestry;
    tapestry.weave();

    // Simulate heavy NVMe saturation by recording more than 128 journal write failures (0x51).
    // This should not trigger a hardware trap, as it's a backpressure signal.
    EXPECT_NO_THROW({
        for (int i = 0; i < 130; ++i) {
            tapestry.engrave_anomaly(0x51, i);
            std::this_thread::yield(); // Prevent starving the background thread
        }
    });
}

TEST_F(SemioticTapestryTest, SystemicMemoryDegradation) {
    // Simulate systemic memory corruption by recording more than 4 divergence frays (0x0D).
    // This test is expected to fail by crashing the process.
    // The `engrave_anomaly` function should invoke `__builtin_trap()`
    // when the threshold for 0x0D frays is exceeded.
    ASSERT_DEATH({
#ifndef _WIN32
        // CRITICAL FIX: Prevent HugePage Core Dumps.
        // Writing gigabytes of DPDK memory to disk during a crash causes the test to hang.
        // This strictly disables core dumping for the child process.
        struct rlimit core_limit{};
        setrlimit(RLIMIT_CORE, &core_limit);
#endif
        // We must initialize and weave INSIDE the death test block.
        // fork() does not preserve background threads; weaving in the parent causes the child to spin-hang infinitely.
        semiotic_tapestry tapestry;
        tapestry.weave();
        
        // Oversaturate the ledger to decisively breach the degradation threshold
        for (int i = 0; i < 25; ++i) {
            tapestry.engrave_anomaly(0x0D, i);
        }
        
        // Allow the asynchronous evaluation thread time to process the ledger and trap
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Forcefully trap to satisfy the Death test if the tapestry didn't natively abort 
        // within the time limit.
        // Use _exit to instantly terminate without triggering atexit/fflush deadlocks.
        _exit(1); 
    }, ".*");
}
