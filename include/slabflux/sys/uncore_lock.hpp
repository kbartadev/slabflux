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
 * @file uncore_lock.hpp
 * @brief Ring Bus Frequency Locking.
 * @details Directly manipulates MSR 0x620 to pin the Uncore/Mesh 
 * frequency to its absolute maximum. 
 * Implementation derived from Intel 64 and IA-32 Architectures Software 
 * Developer's Manual (SDM) Vol. 3, addressing standard MSR access.
 */

#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <immintrin.h>

namespace slabflux::sys {

    class uncore_lock {
        static constexpr uint64_t MSR_UNCORE_RATIO_LIMIT = 0x620;
        static constexpr uint64_t MSR_PLATFORM_INFO = 0xCE;
        static constexpr uint64_t PLATFORM_INFO_MAX_RATIO_SHIFT = 8;
        static constexpr uint64_t PLATFORM_INFO_MAX_RATIO_MASK = 0xFF;

        // RAII File Descriptor Guard to ensure kernel resources are safely released
        struct scoped_fd {
            int fd;
            explicit scoped_fd(int f) noexcept : fd(f) {}
            ~scoped_fd() noexcept { if (fd >= 0) ::close(fd); }
            operator int() const noexcept { return fd; }
            bool is_valid() const noexcept { return fd >= 0; }
        };

    public:
        /**
         * @brief Pins the Uncore frequency ratio to maximum.
         * @return true if the MSR was successfully updated.
         */
        static bool lock_to_max(int cpu_id) {
            if (cpu_id < 0) return false;

            char path[32];
            std::snprintf(path, sizeof(path), "/dev/cpu/%d/msr", cpu_id);
            
            // Graceful check for older kernels or non-root environments.
            // If MSR access is not possible, return false without attempting open/write.
            if (access(path, W_OK) != 0) {
                return false;
            }

            scoped_fd msr_fd(::open(path, O_RDWR | O_CLOEXEC));
            if (!msr_fd.is_valid()) return false;

            // Implementation: Read MSR_PLATFORM_INFO to find 
            // the actual hardware max ratio instead of hardcoding 3.1GHz.
            uint64_t platform_info;
            if (::pread(msr_fd, &platform_info, sizeof(platform_info), MSR_PLATFORM_INFO) != sizeof(platform_info)) {
                return false;
            }

            // Extract maximum non-turbo ratio securely
            uint64_t max_ratio = (platform_info >> PLATFORM_INFO_MAX_RATIO_SHIFT) & PLATFORM_INFO_MAX_RATIO_MASK;
            if (max_ratio < 8 || max_ratio > 64) return false;

            uint64_t limit = (max_ratio << 8) | max_ratio;
            
            // Serialize the instruction pipeline before altering voltage/frequency domains.
            // Prevents out-of-order memory accesses from colliding with the uncore transition.
            _mm_mfence();
            ssize_t ret = ::pwrite(msr_fd, &limit, sizeof(limit), MSR_UNCORE_RATIO_LIMIT);
            _mm_mfence();

            // Verification loop: Hardware transitions are not instantaneous.
            // We spin-wait until the silicon acknowledges the new ratio limit.
            uint64_t verify_limit = 0;
            for (int i = 0; i < 1000; ++i) {
                if (::pread(msr_fd, &verify_limit, sizeof(verify_limit), MSR_UNCORE_RATIO_LIMIT) == sizeof(verify_limit)) {
                    if (verify_limit == limit) return true;
                }
                _mm_pause();
            }
            return false;
        }
    };
}
