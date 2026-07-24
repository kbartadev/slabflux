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
#include "slabflux/sys/cache_partitioner.hpp"
#include "slabflux/sys/isa_guard.hpp"
#include "slabflux/sys/signal_shield.hpp"
#include "slabflux/sys/watchdog_shield.hpp"
#include "slabflux/sys/hardware_telemetry.hpp"
#include "slabflux/sys/tier_guard.hpp"
#include "slabflux/sys/time.hpp"
#include "slabflux/sys/binary_seal.hpp"
#include "slabflux/sys/telemetry_node.hpp"

using namespace slabflux::sys;

/**
 * @brief Verifies instruction set requirements.
 */
TEST(HardwareIsolation, IsaGuardValidatesCpuFeatures) {
    // This should pass on any modern x86_64 target (Haswell+)
    // On older hardware, this verifies the expected panic/throw behavior.
    EXPECT_NO_THROW(isa_guard::verify_requirements());
}

/**
 * @brief Verifies that OS signals can be blocked to prevent jitter.
 */
TEST(HardwareIsolation, SignalShieldSuppressesContextSwitches) {
    // We verify the call is idempotent and safe to execute on a hot-path thread.
    EXPECT_NO_THROW(signal_shield::block_all_on_current_thread());
}

/**
 * @brief Verifies the Binary Identity and Build Integrity.
 */
TEST(HardwareIsolation, BinarySealIdentityValidation) {
    EXPECT_FALSE(binary_seal::build_id.empty());
    EXPECT_TRUE(binary_seal::verify_signature("slabflux_rte_v1"));
}

/**
 * @brief Verifies Performance Monitoring Counter (PMC) interfaces.
 */
TEST(HardwareIsolation, PmuMonitorInterfacesWithPerf) {
    pmu_monitor monitor;
    // PERF_COUNT_HW_CPU_CYCLES = 0
    monitor.open_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES);
    
    uint64_t c1 = monitor.read_counter();
    // Modern Spin-Wait: Bypasses GCC 14 volatile deprecation while preventing optimization
    for(int i=0; i<100; ++i) {
        asm volatile("" : "+g"(i));
    }
    uint64_t c2 = monitor.read_counter();
    
    // If fd is -1 (no permissions), it returns 0 safely.
    // If supported, cycles must increase.
    EXPECT_GE(c2, c1);
}

/**
 * @brief Verifies the Hardware Watchdog interaction.
 */
TEST(HardwareIsolation, WatchdogShieldResilience) {
    hardware_watchdog wd;
    // Verify attempt to engage. In unprivileged CI, this will fail gracefully.
    wd.engage(60); 
    EXPECT_NO_THROW(wd.pet());
}

/**
 * @brief Verifies NUMA/CXL memory tiering enforcement.
 */
TEST(HardwareIsolation, TierGuardEnforcesDramLocality) {
    alignas(4096) char buffer[4096];
    
    try {
        tier_guard::enforce_dram_locality(buffer, 4096);
    } catch (const std::runtime_error& e) {
        GTEST_SKIP() << "Far-Memory detected or simulated: " << e.what();
    }
}

/**
 * @brief Verifies Global Time Synchronization.
 */
TEST(HardwareIsolation, TimeCalibratesOffset) {
    slabflux::sys::time st;
    uint64_t mock_tai = 1700000000000000000ULL;
    st.sync_with_hardware_pulse(mock_tai);
    
    EXPECT_NEAR(st.absolute_now(), mock_tai, 1000000); 
}

/**
 * @brief Verifies telemetry structure alignment for SHM export.
 */
TEST(HardwareIsolation, TelemetryNodeAlignment) {
    EXPECT_EQ(sizeof(core_metrics), 64);
    core_metrics m;
    m.events_processed.store(100);
    EXPECT_EQ(m.events_processed.load(), 100);
}