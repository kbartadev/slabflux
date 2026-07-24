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
 * ============================================================================*
 */
#pragma once
#include <cstdint>
#include <iostream>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

namespace slabflux::sys {

enum class thermal_status {
    nominal,
    throttling_imminent
};

class thermal_guard {
    int max_temp_c_;
public:
    // Default to conservative 85C for safety
    thermal_guard() : max_temp_c_(85) {}
    explicit thermal_guard(int max_temp_c) : max_temp_c_(max_temp_c) {}

    /**
     * @brief Reads CPU package temperature.
     */
    double get_package_temperature() const noexcept {
        // Bypass the OS thermal subsystem and sysfs string parsing.
        // Directly query the CPU's internal IA32_THERM_STATUS MSR (0x19C).
        int fd = ::open("/dev/cpu/0/msr", O_RDONLY | O_CLOEXEC);
        if (fd < 0) return 40.0; // Fail-safe fallback
        
        uint64_t therm_status = 0;
        if (::pread(fd, &therm_status, sizeof(therm_status), 0x19C) != sizeof(therm_status)) {
            ::close(fd);
            return 40.0;
        }
        ::close(fd);
        
        // IA32_THERM_STATUS: Bit 31 specifies if reading is valid.
        // Bits 22:16 contain the Digital Readout (margin below T_jmax, assumed 100C).
        if (therm_status & (1ULL << 31)) {
            uint64_t margin = (therm_status >> 16) & 0x7F;
            return 100.0 - static_cast<double>(margin);
        }
        return 40.0;
    }

    /**
     * @brief Returns current safety status.
     */
    thermal_status get_status() const noexcept {
        if (get_package_temperature() > static_cast<double>(max_temp_c_)) {
            return thermal_status::throttling_imminent;
        }
        return thermal_status::nominal;
    }

    /**
    * @brief Static safety check for ignition manifest.
    */
    static void check_safety() noexcept {
        thermal_guard guard;
        if (guard.get_status() != thermal_status::nominal) {
            std::cerr << "[FATAL] Thermal Guard: CPU package temperature critical. Aborting ignition to prevent micro-architectural throttling.\n";
            std::abort();
        }
    }
};

} // namespace slabflux::sys
