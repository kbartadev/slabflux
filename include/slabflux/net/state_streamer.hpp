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
 * ============================================================================*/
 
#pragma once

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdexcept>
#include <string>
#include <liburing.h>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/security/kinetic_inscription.hpp"

namespace slabflux::net {

    /**
     * @brief Cold-Standby State Archiver.
     * @details Asynchronously streams large memory segments (Retransmission Buffers)
     * to NVMe using io_uring and SQPOLL. Bypasses blocking syscalls on the hot path
     * and integrates Teleological Agnosia for failure handling.
     */
    template<typename T>
    class alignas(64) state_streamer {
        int fd_{-1};
        size_t size_{0};

        struct io_uring ring_;
        bool archive_in_flight_{ false };
        const security::semiotic_tapestry* tapestry_{ nullptr };

        using agnosia_sink_t = void (*)(state_streamer*, uint8_t);
        agnosia_sink_t aphasic_horizon_[256];

        static void execute_valid_stream(state_streamer*, uint8_t) noexcept {}
        
        static void execute_void_stream(state_streamer* st, uint8_t fray) noexcept {
            // Kinetic Inscription: Engrave io_uring submission failure natively
            if (st->tapestry_) {
                st->tapestry_->engrave_anomaly(fray, 0); // Out-of-band execution context
            }
        }

    public:
        state_streamer(const char* path, size_t size) : size_(size) {
            // Dropped O_SYNC. Let io_uring SQPOLL handle the background DMA flush natively
            fd_ = ::open(path, O_RDWR | O_CREAT | O_DIRECT, 0644);
            if (fd_ < 0) throw std::runtime_error("state_streamer: open failed");
            if (::ftruncate(fd_, size) != 0) throw std::runtime_error("state_streamer: truncate failed");

            io_uring_params params{};
            params.flags |= IORING_SETUP_SQPOLL;
            if (io_uring_queue_init_params(8, &ring_, &params) < 0) {
                throw std::runtime_error("state_streamer: io_uring init failed");
            }

            aphasic_horizon_[0] = &execute_valid_stream;
            for (int i = 1; i < 256; ++i) {
                aphasic_horizon_[i] = &execute_void_stream;
            }
        }

        void bind_tapestry(const security::semiotic_tapestry* tapestry) noexcept {
            tapestry_ = tapestry;
        }

        ~state_streamer() { 
            io_uring_queue_exit(&ring_);
            if (fd_ >= 0) ::close(fd_); 
        }

        SLAB_HOT void archive(const T& data) noexcept {
            if (SL_EXPECT_FALSE(archive_in_flight_)) {
                struct io_uring_cqe* cqe;
                if (io_uring_peek_cqe(&ring_, &cqe) == 0) {
                    io_uring_cqe_seen(&ring_, cqe);
                    archive_in_flight_ = false;
                } else {
                    return; // Previous DMA transfer still in progress
                }
            }

            struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
            uint8_t fray = 0;
            
            if (SL_EXPECT_TRUE(sqe)) {
                io_uring_prep_write(sqe, fd_, &data, size_, 0);
                io_uring_submit(&ring_);
                archive_in_flight_ = true;
            } else {
                fray = 0x51; // Submission Fault (mapped to generic I/O fray code)
            }

            // Teleological Agnosia: Drops the stream natively if io_uring is saturated
            aphasic_horizon_[fray](this, fray);
        }
    };
}