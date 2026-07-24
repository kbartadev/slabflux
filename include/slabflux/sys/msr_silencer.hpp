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
 * @file msr_silencer.hpp
 * @brief Disabling Hardware Prefetchers.
 * @details Directly manipulates MSR 0x1A4 to prevent the CPU from 
 * making "guesses" that pollute the L1/L2 cache.
 */

#pragma once

#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <sched.h>

namespace slabflux::sys {

    class msr_silencer {
        // Intel MSR for Prefetch Control
        static constexpr off_t MSR_PREFETCH_CONTROL = 0x1A4;
        
        // Bits 0-3 disable L2 hardware, L2 IP, DCU hardware, and DCU IP prefetchers
        static constexpr uint64_t DISABLE_PREFETCH_MASK = 0x0F;

    public:
        /**
         * @brief Checks if MSR manipulation is supported on this host.
         */
        static bool is_supported() noexcept {
            // Check for driver availability
            if (::access("/dev/cpu/0/msr", F_OK) != 0) return false;
            
            char path[64];
            std::snprintf(path, sizeof(path), "/dev/cpu/%d/msr", ::sched_getcpu());
            
            // Verify we have permission to write prefetcher controls
            return ::access(path, W_OK) == 0;
        }

        /**
         * @brief Disables all hardware prefetchers on the specific core.
         */
        static void silence_prefetchers(int cpu_id) noexcept {
            char path[64];
            std::snprintf(path, sizeof(path), "/dev/cpu/%d/msr", cpu_id);
            int fd = ::open(path, O_RDWR | O_CLOEXEC);
            if (fd < 0) return;
            
            uint64_t val = 0;
            if (::pread(fd, &val, sizeof(val), MSR_PREFETCH_CONTROL) == sizeof(val)) {
                val |= DISABLE_PREFETCH_MASK;
                ::pwrite(fd, &val, sizeof(val), MSR_PREFETCH_CONTROL);
            }
            
            ::close(fd);
        }
    };
}
