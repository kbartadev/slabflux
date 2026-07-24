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
#include <liburing.h>
#include "entity_slab.hpp"
#include "sf_node_ctx.hpp"
#include "slabflux/core/integrity_validator.hpp"

namespace slabflux::net {

/**
 * @brief Handles bit-perfect state transfers between nodes.
 * @details Used only during Critical Desync. Bypasses the LSN pipe to 
 * perform a bulk memory clone.
 */
template<typename SlabType>
class snapshot_sync {
    struct alignas(16) snapshot_header {
        uint64_t lsn;
        uint64_t state_hash;
    };

    SlabType& local_slab_;
    slabflux::core::sf_node_ctx& context_;
    snapshot_header remote_header_{0, 0};

public:
    snapshot_sync(SlabType& slab, slabflux::core::sf_node_ctx& ctx) 
        : local_slab_(slab), context_(ctx) {}

    /**
     * @brief Receive a full state snapshot from a peer.
     * @details Fully asynchronous kernel-linked operations (IOSQE_IO_LINK).
     */
    void ingest_snapshot_async(int peer_fd, struct io_uring& ring) noexcept {
        // Optimization: NEVER use blocking syscalls (recv/wait) in an SQPOLL engine.
        // We chain the header read and the body read in the kernel using IOSQE_IO_LINK.
        
        struct io_uring_sqe* sqe1 = io_uring_get_sqe(&ring);
        io_uring_prep_recv(sqe1, peer_fd, &remote_header_, sizeof(remote_header_), MSG_WAITALL);
        sqe1->flags |= IOSQE_IO_LINK; // Kernel MUST execute SQE2 immediately after this completes
        sqe1->user_data = 0; 

        struct io_uring_sqe* sqe2 = io_uring_get_sqe(&ring);
        io_uring_prep_recv(sqe2, peer_fd, &local_slab_, sizeof(local_slab_), MSG_WAITALL);
        sqe2->user_data = 0x5452414E; // "TRAN" - Signals Nexus to finalize the transfer in CQE poll
        
        io_uring_submit(&ring);
    }

    /**
     * @brief Called by CQE poll when user_data matches 0x5452414E.
     */
    void finalize_transfer() noexcept {
        // Integrity: Cryptographic/Hardware validation of the bulk state before committing
        uint64_t computed_hash = slabflux::core::integrity_validator::compute_checksum(&local_slab_, sizeof(local_slab_));
        
        if (SL_EXPECT_FALSE(computed_hash != remote_header_.state_hash)) {
            slabflux::core::handle_critical_error("Snapshot Sync: State corruption detected during bulk transfer!");
        }

        context_.current_lsn.store(remote_header_.lsn, std::memory_order_release);
        context_.committed_lsn.store(remote_header_.lsn, std::memory_order_release);
    }
};

} // namespace slabflux::net