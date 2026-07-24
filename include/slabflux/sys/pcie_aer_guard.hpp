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
 *
 * @file pcie_aer_guard.hpp
 * @brief Hardware Bus Health Monitoring.
 */

#pragma once

#include <linux/pci_regs.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <immintrin.h>

namespace slabflux::sys {

    class pcie_aer_guard {
    public:
        /**
         * @brief Checks for PCIe AER (Advanced Error Reporting) flags.
         */
        static bool is_bus_healthy(const char* device_path) {
            int fd = open(device_path, O_RDONLY | O_CLOEXEC);
            if (fd < 0) return true; // Fail-open for non-privileged testing
            
            // Zero-Syscall ECAM Parsing.
            // Bulk-read the entire 4KB Extended Configuration Space into L1 cache 
            // and traverse it in user-space to avoid repeated pread() syscalls.
            alignas(64) uint8_t pci_config_space[4096];
            if (::pread(fd, pci_config_space, sizeof(pci_config_space), 0) < 0x100) {
                ::close(fd);
                return true;
            }
            ::close(fd);

            uint32_t aer_offset = 0x100; // PCIe Extended Capabilities always start at 0x100
            while (aer_offset != 0 && aer_offset < (4096 - 0x14)) {
                // L1 Cache hit - zero kernel overhead
                uint32_t header = *reinterpret_cast<const uint32_t*>(&pci_config_space[aer_offset]);
                
                if ((header & 0xFFFF) == 0x0001) { // AER ID
                    uint32_t status = *reinterpret_cast<const uint32_t*>(&pci_config_space[aer_offset + 0x10]);
                    return status == 0;
                }
                aer_offset = (header >> 20) & 0xFFF; // Next capability pointer
            }

            return true; // Assume healthy if AER capability isn't found
        }
    };
}
