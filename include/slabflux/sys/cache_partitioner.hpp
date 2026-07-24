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
 * @file cache_partitioner.hpp
 * @brief L3 Cache Set Partitioning.
 * @details Prevents cache eviction between network buffers and domain state 
 * by using Intel CAT (Cache Allocation Technology) via resctrl.
 */

#pragma once

#include <string>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <algorithm>

namespace slabflux::sys {

    class cache_partitioner {
        // RAII File Descriptor Guard for low-level deterministic I/O
        struct scoped_fd {
            int fd;
            explicit scoped_fd(int f) noexcept : fd(f) {}
            ~scoped_fd() noexcept { if (fd >= 0) ::close(fd); }
            operator int() const noexcept { return fd; }
            bool is_valid() const noexcept { return fd >= 0; }
        };

    public:
        /**
         * @brief Checks if the resctrl infrastructure is available.
         */
        static bool is_supported() noexcept {
            return access("/sys/fs/resctrl", F_OK) == 0;
        }

        /**
         * @brief Assigns a dedicated L3 cache portion to the current process.
         * @details Zero-Allocation Kernel Configurator.
         * Bypasses textbook std::string heap allocations used in generic tutorials,
         * utilizing strictly bounded stack buffers to prevent memory fragmentation.
         * @param group_name The resctrl group identifier.
         * @param intensity The desired partition depth.
         */
        static void enforce_exclusive_l3(const std::string& group_name, uint32_t intensity) {
            // Requires Linux resctrl (Resource Control) filesystem
            if (!is_supported()) return;

            uint32_t ways = std::max(2u, std::min(intensity, 20u));
            uint32_t mask = (1 << ways) - 1;

            char path[256];
            char payload[64];

            // 1. Submit the Capacity Bitmask (CBM)
            std::snprintf(path, sizeof(path), "/sys/fs/resctrl/%s/schemata", group_name.c_str());
            scoped_fd s_fd(::open(path, O_WRONLY | O_CLOEXEC));
            if (s_fd.is_valid()) {
                int len = std::snprintf(payload, sizeof(payload), "L3:0=%x\n", mask);
                [[maybe_unused]] auto res = ::write(s_fd, payload, len);
            }
            
            // 2. Bind the current process PID to the isolated physical cache group
            std::snprintf(path, sizeof(path), "/sys/fs/resctrl/%s/tasks", group_name.c_str());
            scoped_fd t_fd(::open(path, O_WRONLY | O_CLOEXEC));
            if (t_fd.is_valid()) {
                int len = std::snprintf(payload, sizeof(payload), "%d\n", ::getpid());
                [[maybe_unused]] auto res = ::write(t_fd, payload, len);
            }
        }
    };
}
