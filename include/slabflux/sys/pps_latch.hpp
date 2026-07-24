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
 * @file pps_latch.hpp
 * @brief Hardware PTP pulse latching.
 */

#pragma once

#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

namespace slabflux::sys {

    /** @brief Local hardware PTP contract (Standard Linux interface). */
    struct ptp_clock_time { int64_t sec; uint32_t nsec; uint32_t reserved; };
    struct ptp_sys_offset_extended {
        uint32_t n_samples;
        uint32_t reserved[3];
        ptp_clock_time ts[25][3];
    };

    #ifndef PTP_SYS_OFFSET_EXTENDED
    #define PTP_SYS_OFFSET_EXTENDED _IOWR('=', 9, struct ptp_sys_offset_extended)
    #endif

    struct pps_calibration {
        uint64_t tsc_at_pulse;
        uint64_t ptp_ns_at_pulse;
    };

    class pps_latch {
        int ptp_fd_{-1};

        static inline uint64_t ptp_to_ns(const ptp_clock_time& ts) noexcept {
            return static_cast<uint64_t>(ts.sec) * 1000000000ULL + ts.nsec;
        }

    public:
        ~pps_latch() {
            if (ptp_fd_ >= 0) close(ptp_fd_);
        }

        pps_calibration capture_pulse() noexcept {
            if (ptp_fd_ < 0) {
                ptp_fd_ = open("/dev/ptp0", O_RDWR);
            }

            if (ptp_fd_ >= 0) {
                ptp_sys_offset_extended prec;
                prec.n_samples = 5;
                if (::ioctl(ptp_fd_, PTP_SYS_OFFSET_EXTENDED, &prec) == 0) {
                    return { ptp_to_ns(prec.ts[2][0]), ptp_to_ns(prec.ts[2][1]) };
                }
            }
            return { 0, 0 }; 
        }
    };
}
