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
 * @file lbr_analyzer.hpp
 * @brief Branch Trace Reconstruction.
 * @details Direct access to the Last Branch Record stack to 
 * analyze speculative execution paths.
 */

#pragma once

#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <cstdio>
#include "slabflux/sys/isa_guard.hpp"

namespace slabflux::sys {

    struct lbr_entry {
        uint64_t from_ip;
        uint64_t to_ip;
        bool mispredicted;
    };

    class lbr_analyzer {
    public:
        /**
         * @brief Captures the current LBR stack.
         * @param depth Number of entries to capture (Architecture dependent).
         * @note Must be called via MSR read on the Core.
         */
        static void snapshot_history(lbr_entry* out_buffer) noexcept {
            auto f = isa_guard::detect();
            if (!f.has_lbr_capabilities) {
                return;
            }

            char path[32];
            snprintf(path, sizeof(path), "/dev/cpu/%d/msr", sched_getcpu());

            int fd = open(path, O_RDONLY);
            if (fd < 0) return;

            // Discovery: Read IA32_LBR_DEPTH (0x14E0) on modern Intel
            uint64_t depth = 16; 
            pread(fd, &depth, sizeof(depth), 0x14E0);
            
            // TOS (Top of Stack) Discovery
            uint64_t tos;
            if (pread(fd, &tos, sizeof(tos), 0x1C9) != sizeof(tos)) {
                close(fd);
                return;
            }

            // Capture up to depth or buffer size
            for (uint64_t i = 0; i < 16 && i < depth; ++i) {
                uint64_t from, to;
                // MSR_LASTBRANCH_0_FROM_IP starts at 0x680
                // MSR_LASTBRANCH_0_TO_IP starts at 0x6C0
                if (pread(fd, &from, sizeof(from), 0x680 + i) != sizeof(from)) break;
                if (pread(fd, &to, sizeof(to), 0x6C0 + i) != sizeof(to)) break;
                
                out_buffer[i] = { from, to, false };
            }
            close(fd);
        }
    };
}
