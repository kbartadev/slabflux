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
#include <immintrin.h>
#include <thread>
#include <atomic>
#ifndef _WIN32
#include <sys/resource.h>
#endif
#include "slabflux/sys/fpu_shield.hpp"
#include "slabflux/sys/power_governor.hpp"
#include "slabflux/sys/liveness_watchdog.hpp"

using namespace slabflux::sys;

/**
 * @brief Test for FPU Shielding.
 * Verifies that FTZ (Flush-to-Zero) and DAZ (Denormals-are-Zero) bits are 
 * correctly set in the MXCSR register to prevent subnormal stalling.
 */
TEST(ProcessorDeterminism, FpuShieldEnforcesSubnormalSanitization) {
    fpu_shield shield;

    uint32_t mxcsr = _mm_getcsr();
    
    // Bit 15: Flush-to-Zero (FTZ)
    // Bit 6: Denormals-are-Zero (DAZ)
    constexpr uint32_t FTZ_BIT = (1 << 15);
    constexpr uint32_t DAZ_BIT = (1 << 6);

    EXPECT_NE(mxcsr & FTZ_BIT, 0) << "FTZ bit not set in MXCSR!";
    EXPECT_NE(mxcsr & DAZ_BIT, 0) << "DAZ bit not set in MXCSR!";
}

/**
 * @brief Test for Liveness Watchdog.
 * Validates that the TSC (Time Stamp Counter) progress is monitored 
 * and that cycle-budget violations are detectable.
 */
TEST(ProcessorDeterminism, WatchdogDetectsCoreHang) {
    // The supervisor core should detect the hang and panic
    EXPECT_DEATH({
#ifndef _WIN32
        // Disable core dumps to prevent HugePage flushing hangs on Death Tests
        struct rlimit core_limit{};
        setrlimit(RLIMIT_CORE, &core_limit);
#endif
        // Seed with current TSC to prevent instant false-positive before the thread boots
        std::atomic<uint64_t> tsc_marker{__rdtsc()};
        liveness_watchdog watchdog(50'000'000, tsc_marker); // 50M cycle budget (~16ms @ 3GHz)
        
        // Simulate a core that updates its marker, then stops.
        std::thread monitored_core([&]() {
            tsc_marker.store(__rdtsc(), std::memory_order_release);
            // Simulate work, then a hang (no further marker updates)
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); 
            // The core "hangs" here, not updating tsc_marker
            while(true) { _mm_pause(); } 
        });
        
        // Mandatory yield to ensure the monitored_core initializes its marker 
        // before the supervisor loop saturates the local execution unit.
        std::this_thread::yield();
        watchdog.monitor();
        
        monitored_core.join(); // Should never be reached if monitor() aborts as expected
    }, "Liveness budget exceeded - Core Hang detected");
}

/**
 * @brief Verifies graceful shutdown of the watchdog.
 */
TEST(ProcessorDeterminism, WatchdogShutsDownGracefully) {
    // Seed with current TSC and use a massive budget so OS scheduling doesn't trigger a false positive
    std::atomic<uint64_t> tsc_marker{__rdtsc()};
    liveness_watchdog watchdog(1'000'000'000, tsc_marker);

    std::thread monitor_thread([&]() {
        watchdog.monitor();
    });

    watchdog.stop();
    monitor_thread.join();
    SUCCEED(); // If it joins, it shut down gracefully
}

/**
 * @brief Test for Power Governance.
 * Ensures that the system is primed for AVX-512 execution to avoid 
 * frequency scaling penalties during the first vector instructions.
 */
TEST(ProcessorDeterminism, PowerGovernorPrimesInstructionLanes) {
    power_governor gov;
    
    // Verify that the governor can lock C-states (requires root in prod)
    // Here we test the AVX-512 warm-up sequence.
    auto status = gov.prime_vector_units();
    
    #if defined(__AVX512F__)
        EXPECT_TRUE(status == power_governor::status::primed);
    #else
        // If the hardware doesn't support AVX-512, it should degrade gracefully
        EXPECT_TRUE(status == power_governor::status::unsupported || 
                    status == power_governor::status::primed);
    #endif
}
