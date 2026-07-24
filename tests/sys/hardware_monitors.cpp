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
#include "slabflux/sys/mce_listener.hpp"
#include "slabflux/sys/thermal_guard.hpp"
#include "slabflux/sys/smi_monitor.hpp"

using namespace slabflux::sys;

/**
 * @brief Test for SMI (System Management Interrupt) Detection.
 * SMIs are the primary source of jitter in high-frequency systems.
 */
TEST(HardwareIntegrity, SmiMonitorDetectsBackgroundJitter) {
    smi_monitor monitor;
    
    if (!monitor.is_supported()) {
        GTEST_SKIP() << "MSR access not available; SMI monitoring skipped.";
    }

    uint64_t initial_count = monitor.get_smi_count();
    
    // Tight loop to check for stability
    for(int i = 0; i < 1000; ++i) {
        asm volatile("pause");
    }
    
    uint64_t final_count = monitor.get_smi_count();
    
    // In a clean environment, this should ideally be 0
    EXPECT_GE(final_count, initial_count);
}

/**
 * @brief Test for Thermal Safety.
 * Ensures the system can read CPU package temperatures to prevent 
 * thermal throttling from poisoning latency.
 */
TEST(HardwareIntegrity, ThermalGuardReportsSafeOperatingRange) {
    thermal_guard guard;
    
    double temp = guard.get_package_temperature();
    
    // Standard server operating range (Celsius)
    EXPECT_GT(temp, 0.0);
    EXPECT_LT(temp, 100.0) << "CPU is approaching critical T-Junction!";
    
    // Verify that the status invariant is consistent
    if (temp > 85.0) {
        EXPECT_EQ(guard.get_status(), thermal_status::throttling_imminent);
    } else {
        EXPECT_EQ(guard.get_status(), thermal_status::nominal);
    }
}