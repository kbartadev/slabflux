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

#ifdef __linux__
#include <liburing.h>
#endif
#include <x86intrin.h>
#include <stdexcept>
#include "wire_frame_lsn.hpp"
#include "slabflux/core/sequence_generator.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/sys/mce_listener.hpp"
#include "slabflux/security/kinetic_inscription.hpp"

namespace slabflux::net {

/**
 * @brief Bare-Metal SHM Ingress Engine.
 * @details Consolidates high-frequency io_uring mechanics with authoritative 
 * logical sequence numbering to achieve zero-syscall network ingestion 
 * directly into Shared Memory (SHM) frames.
 * 
 * Hardware Integration:
 * - Zero-Copy Path: Reaps frames directly from the SHM pool, bypassing 
 *   kernel-to-user memory copies.
 * - SQPOLL/SQ_AFF: Offloads ring submission to a kernel-side polling thread 
 *   pinned to a physically local core.
 * - Deterministic LSN: Performs authoritative logical sequence numbering 
 *   immediately upon completion queue reaping to preserve causality.
 */
class alignas(64) server_ingress {
    struct io_uring ring_;
    core::sequence_generator lsn_gen_;
    sys::mce_listener hardware_monitor_;
    bool initialized_ = false;
    const security::semiotic_tapestry* tapestry_{ nullptr };

    template <typename Bridge>
    static void execute_valid_ingress(server_ingress*, struct io_uring& ring, core::sequence_generator& lsn_gen, Bridge& bridge, uint8_t) noexcept {
        struct io_uring_cqe* cqe;
        unsigned head;
        int count = 0;

        // HFT Jitter Fix: Never execute blocking syscalls in the hot path!
        // If the CQE ring is empty, instantly return to the event loop.
        if (SL_EXPECT_FALSE(!io_uring_cq_ready(&ring))) {
            return;
        }

        io_uring_for_each_cqe(&ring, head, cqe) {
            auto* frame = static_cast<wire_frame_lsn<char>*>(io_uring_cqe_get_data(cqe));
            if (cqe->res > 0) {
                frame->lsn = lsn_gen.next();
                frame->ingress_ts = __rdtsc();
            }
            // Pass to bridge unconditionally to ensure zero memory leaks.
            // Negative res indicates hardware fault; bridge must release the frame.
            bridge.on_raw_frame(frame, cqe->res);
            count++;
        }
        if (count > 0) io_uring_cq_advance(&ring, count);
    }

    template <typename Bridge>
    static void execute_void_ingress(server_ingress* ing, struct io_uring&, core::sequence_generator& lsn_gen, Bridge&, uint8_t fray) noexcept {
        // Kinetic Inscription: LBR engraving for Machine Check Exception faults
        if (ing->tapestry_) {
            ing->tapestry_->engrave_anomaly(fray, lsn_gen.current());
        }
    }

public:
    server_ingress(int entries, int sq_core_id = -1, int attach_fd = -1, int cq_entries = 0) {
#ifdef __linux__
        io_uring_params params{};
        if (sq_core_id >= 0) {
            params.flags |= IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
            params.sq_thread_cpu = sq_core_id;
            // Tuning: Increase idle time to 10s to ensure the SQPOLL kthread 
            // remains active during bursty NACK traffic, avoiding wakeup syscall latency.
            params.sq_thread_idle = 10000;
        }
        // Efficiency: DEFER_TASKRUN requires SINGLE_ISSUER.
        params.flags |= IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN | IORING_SETUP_DEFER_TASKRUN;

        // Efficiency: Share the async worker pool (io-wq) if a master ring exists.
        // This reduces kernel thread overhead and improves resource locality.
        if (attach_fd >= 0) {
            params.flags |= IORING_SETUP_ATTACH_WQ;
            params.wq_fd = attach_fd;
        }
        if (cq_entries > 0) {
            params.flags |= IORING_SETUP_CQSIZE;
            params.cq_entries = cq_entries;
        }
        if (io_uring_queue_init_params(entries, &ring_, &params) < 0) {
            throw std::runtime_error("Failed to initialize io_uring for server_ingress");
        }
        initialized_ = true;
#endif
    }

    void bind_tapestry(const security::semiotic_tapestry* tapestry) noexcept {
        tapestry_ = tapestry;
    }

    /**
     * @brief Returns the internal ring file descriptor for attaching other rings.
     */
    int get_ring_fd() const noexcept { return ring_.ring_fd; }

    /**
     * @brief Returns the raw io_uring structure for advanced buffer ring registrations.
     */
    struct io_uring& get_raw_ring() noexcept { return ring_; }

    ~server_ingress() {
#ifdef __linux__
        if (initialized_) io_uring_queue_exit(&ring_);
#endif
    }

    /**
     * @brief Non-blocking poll of the completion queue.
     * @param bridge A functional or object bridge that implements on_raw_frame.
     */
    void poll_clients(auto& bridge) {
#ifdef __linux__
        using BridgeType = std::decay_t<decltype(bridge)>;
        using sink_t = void (*)(server_ingress*, struct io_uring&, core::sequence_generator&, BridgeType&, uint8_t);
        const sink_t aphasic_horizon_[2] = { &execute_valid_ingress<BridgeType>, &execute_void_ingress<BridgeType> };

        // BITALG Silicon Shearing yields 0 on success, >0 on Fray.
        uint8_t fray = hardware_monitor_.check_health() ? 0 : 1;

        // Teleological Agnosia
        aphasic_horizon_[fray](this, ring_, lsn_gen_, bridge, fray);
#else
        (void)bridge;
#endif
    }
};

} // namespace slabflux::net
