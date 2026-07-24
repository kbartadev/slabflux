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
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <x86intrin.h>
#include <liburing.h>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::sys {

    /**
    * @brief Hardware exception monitor.
    * @details Interfaces with /dev/mcelog to detect fatal machine check exceptions.
    */
    class mce_listener {
    int fd_{-1};
    uint64_t last_check_tsc_{0};
    static constexpr uint64_t MCE_POLL_INTERVAL_CYCLES = 3'000'000ULL; // ~1ms at 3GHz
    struct io_uring ring_;
    alignas(64) uint8_t opaque_frame_[40];
    bool read_inflight_{false};

    public:
        mce_listener() noexcept {
            fd_ = ::open("/dev/mcelog", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd_ != -1) {
                // Zero-Syscall MCE Observation.
                // Replaces standard blocking or non-blocking read() calls with SQPOLL-driven 
                // io_uring submissions, isolating the VFS latency from the critical execution path.
                io_uring_queue_init(2, &ring_, 0);
                submit_read();
            }
        }

        ~mce_listener() noexcept {
            if (fd_ != -1) {
                io_uring_queue_exit(&ring_);
                ::close(fd_);
            }
        }

        /**
        * @brief Detects uncorrected hardware faults.
        * @return false if a fatal machine check exception is detected.
        */
        SLAB_FORCE_INLINE bool check_health() noexcept {
            if (SL_EXPECT_FALSE(fd_ == -1)) return true;

            struct io_uring_cqe* cqe;
            // Peak memory purely from user-space; Zero syscall context switches.
            if (io_uring_peek_cqe(&ring_, &cqe) == 0) {
                if (cqe->res == sizeof(opaque_frame_)) {
                    uint64_t status_reg = *reinterpret_cast<uint64_t*>(opaque_frame_);
                    // Bit 61: VAL (Status register is valid)
                    // Bit 58: UC  (Uncorrected Error)
                    if ((status_reg & (1ULL << 61)) && (status_reg & (1ULL << 58))) {
                        io_uring_cqe_seen(&ring_, cqe);
                        return false; // Hardware Failure Detected
                    }
                }
                io_uring_cqe_seen(&ring_, cqe);
                submit_read(); // Queue the next check asynchronously
            }
            return true;
        }

    private:
        void submit_read() {
            struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
            if (sqe) {
                io_uring_prep_read(sqe, fd_, opaque_frame_, sizeof(opaque_frame_), 0);
                io_uring_submit(&ring_);
            }
        }
    };

} // namespace slabflux::sys
