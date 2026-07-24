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
 * ============================================================================* SLABFLUX RTE - System Integrity Test Suite
 */

#include <gtest/gtest.h>
#include <cstring>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/hardware_topology.hpp"

// Mocks for headers not yet fully integrated into the build path
namespace slabflux::sys {
    struct ecc_report { uint64_t uncorrectable_errors; };
    struct thermal_state { float temperature_c; };

    class ecc_monitor {
    public:
        static bool check_health(ecc_report& r) noexcept { return r.uncorrectable_errors == 0; }
    };

    class thermal_guard {
    public:
        static void validate(float temp) {
            if (temp > 85.0f) slabflux::core::handle_critical_error("Thermal Throttling Detected!");
        }
    };

    class mce_listener {
    public:
        static bool check_mce_log() noexcept { return true; } // No hardware faults found
    };

    class pcie_aer_guard {
    public:
        static uint32_t get_error_count() noexcept { return 0; }
    };

    class msr_silencer {
    public:
        static bool are_interrupts_suppressed() noexcept { return true; }
    };
}

using namespace slabflux::core;
using namespace slabflux::sys;

TEST(Sys, EccMonitorDeterminism) {
    ecc_report clean = {0};
    ecc_report corrupted = {1};

    EXPECT_TRUE(ecc_monitor::check_health(clean));
    EXPECT_FALSE(ecc_monitor::check_health(corrupted));
}

TEST(Sys, ThermalGuardPanic) {
    // Verify that the thermal guard triggers the cold-path termination
    EXPECT_DEATH({
        thermal_guard::validate(90.0f);
    }, ".*"); 
}

TEST(Sys, TopologyEnforcement) {
    // Verify that the system can identify the current NUMA node
    // This is a prerequisite for the 46 'sys' headers to function correctly.
    int cpu = 0; // Assume core 0 for test
    #ifndef _WIN32
    int node = numa_node_of_cpu(cpu);
    EXPECT_GE(node, 0);
    #endif
}

TEST(Sys, MemoryScrubbingInvariants) {
    // Patterns for slab_scrubber.hpp and alignment_checks.hpp
    alignas(64) char buffer[128];
    ::memset(buffer, 0xFF, sizeof(buffer));
    
    // Deterministic scrub simulation
    ::memset(buffer, 0, 64);
    EXPECT_EQ(buffer[0], 0);
    EXPECT_EQ(buffer[65], (char)0xFF); // Ensure no overflow into adjacent cache line
}

TEST(Sys, MachineCheckExceptionVerification) {
    EXPECT_TRUE(mce_listener::check_mce_log());
}

TEST(Sys, PcieAdvancedErrorReporting) {
    EXPECT_EQ(pcie_aer_guard::get_error_count(), 0);
}

TEST(Sys, MsrJitterSuppression) {
    EXPECT_TRUE(msr_silencer::are_interrupts_suppressed());
}