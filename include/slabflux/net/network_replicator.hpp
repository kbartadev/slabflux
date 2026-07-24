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
 * SlabFlux Chip Core
 * Network Replicator: Zero-Copy Log Scattering.
 */

#pragma once
#include <array>
#include "../core/wire_frame_lsn.hpp"
#include "../hft/zero_copy_egress.hpp"

namespace slabflux::net {

    using namespace slabflux::core;

    /**
     * @brief Event scatterer.
     * @details Performance integrity. Every replica receives the exact same bitstream.
     */
    template<typename Payload>
    class network_replicator {
        // Fixed-size flat matrix for zero-allocation replica tracking
        static constexpr size_t MAX_REPLICAS = 32;
        std::array<int, MAX_REPLICAS> follower_fds_{};
        size_t follower_count_{0};

        io_uring* ring_;

    public:
        explicit network_replicator(io_uring* ring) : ring_(ring) {}

        void add_follower(int fd) noexcept { 
            if (follower_count_ < MAX_REPLICAS) {
                follower_fds_[follower_count_++] = fd;
            }
        }

        /**
         * @brief Scatter the sanctified truth.
         * @details Zero-copy using the io_uring SEND operation.
         */
        inline void scatter(const wire_frame_lsn<Payload>* frame) noexcept {
            for (size_t i = 0; i < follower_count_; ++i) {
                int fd = follower_fds_[i];
                // We do not call send(), we only request an SQE from the ring.
                // The kernel reads the frame directly from RAM.
                unsigned sq_tail = ring_->sq.sqe_tail;
                const unsigned sq_mask = *ring_->sq.kring_mask;
                io_uring_sqe* sqe = &ring_->sq.sqes[sq_tail & sq_mask];

                // True Zero-Copy send bypassing socket buffers
                io_uring_prep_send_zc(sqe, fd, frame, sizeof(*frame), MSG_NOSIGNAL, 0);
                // BUGFIX: fd is a raw descriptor. IOSQE_FIXED_FILE expects an array index.

                ring_->sq.sqe_tail++; // O(1) pointer bump.
            }
        }
    };

} // namespace slabflux::net
