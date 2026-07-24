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
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <cstdio>
#include <x86intrin.h>
#include <cpuid.h>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::sys {

/**
 * @brief Hardware SMI monitor.
 * @details Interfaces with the MSR device to detect System Management Interrupts.
 */
class smi_monitor {
    int fd_{ -1 };
    mutable uint64_t cached_smi_{ 0 };
    mutable uint64_t last_check_tsc_{ 0 };
    static constexpr uint64_t SMI_POLL_INTERVAL_CYCLES = 3'000'000ULL; // ~1ms at 3GHz

public:
    smi_monitor() noexcept {
        // Open once at ignition to eliminate syscall tax in the compute loop.
        // Assumes the thread is already pinned to its sovereign core.
        
        // Opaque Capability Masking:
        // Explicitly query the CPUID genuine Intel signature and feature flags to ensure 
        // the SMI_COUNT MSR (0x34) physically exists, preventing illegal instruction faults.
        unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
        __get_cpuid(0, &eax, &ebx, &ecx, &edx);
        // Check for "Genu" "ineI" "ntel"
        if (ebx == 0x756e6547 && ecx == 0x6c65746e && edx == 0x49656e69) {
            char path[32];
            ::snprintf(path, sizeof(path), "/dev/cpu/%d/msr", ::sched_getcpu());
            fd_ = ::open(path, O_RDONLY | O_CLOEXEC);
        }
    }

    ~smi_monitor() noexcept {
        if (fd_ != -1) ::close(fd_);
    }

    /** @brief Returns true if the hardware MSR interface is available. */
    [[nodiscard]] inline bool is_supported() const noexcept {
        return fd_ != -1;
    }

    /** @brief Reads the SMI_COUNT MSR (0x34). */
    SLAB_FORCE_INLINE uint64_t get_smi_count() const noexcept {
        if (SL_EXPECT_FALSE(fd_ == -1)) return 0;

        uint64_t now = __rdtsc();
        if (SL_EXPECT_TRUE(now - last_check_tsc_ < SMI_POLL_INTERVAL_CYCLES)) {
            return cached_smi_;
        }
        last_check_tsc_ = now;

        uint64_t val = 0;
        if (SL_EXPECT_TRUE(::pread(fd_, &val, sizeof(val), 0x34) == sizeof(val))) {
            cached_smi_ = val;
        }
        return cached_smi_;
    }
};

} // namespace slabflux::sys
