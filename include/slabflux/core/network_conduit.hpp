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
 * @file network_conduit.hpp

 */

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

#include "physical_layout.hpp"
#include "slabflux/core/hot_path_alignment.hpp" // For SLAB_FORCE_INLINE
#include "slabflux/core/spsc_conduit.hpp"

#include "slabflux/transport/wire_protocol.hpp" // Required for transport::raw_tcp_frame

namespace slabflux {
    namespace transport = ::slabflux::transport;
    namespace core { namespace transport = ::slabflux::transport; }
}

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif
#include <cstring>

namespace slabflux::core {

// ============================================================
// SLABFLUX IO: STANDARD EGRESS GATEWAY
// Fallback engine for standard OS socket transmission.
// Matches the API of the io_uring/DPDK Nexus for hot-path swap.
// ============================================================
template <typename TargetConduit, typename PoolType, std::size_t MaxBurstSize = 32>
class alignas(64) standard_egress_gateway {
private:
    int                socket_fd_{-1};
    TargetConduit&     interconnect_conduit_;
    PoolType&          mem_pool_;
    std::atomic<bool>& running_sentinel_;

    // Core-local registers for cycle-accurate tracking
    alignas(64) std::size_t tx_count_{0};
    alignas(64) std::size_t drop_count_{0};

    void (*fault_fn_)(void*, uint32_t){nullptr};
    void* arbiter_ptr_{nullptr};

   public:
    explicit standard_egress_gateway(
        int sock, 
        TargetConduit& conduit, 
        PoolType& pool, 
        std::atomic<bool>& running
    ) noexcept 
    : socket_fd_(sock)
    , interconnect_conduit_(conduit)
    , mem_pool_(pool)
    , running_sentinel_(running) {}

    ~standard_egress_gateway() = default;

    /** @brief Attaches a fault reporter (error_arbiter integration). */
    void attach_fault_reporter(void* arbiter, void (*fn)(void*, uint32_t)) noexcept {
        arbiter_ptr_ = arbiter;
        fault_fn_ = fn;
    }

    /**
     * @brief Vectorized Egress Poll.
     * @details Amortizes syscall overhead by draining the conduit in bursts.
     * Strictly sub-25 cycle iteration cost when the conduit is empty.
     */
    SLAB_HOT void poll_egress() noexcept {
        alignas(64) typename TargetConduit::value_type batch[MaxBurstSize];
        
        // 1. INGEST VECTOR BURST
        // Amortize atomic tax by pulling multiple pointers in one cycle
        std::size_t pulled = interconnect_conduit_.pop_batch(batch, MaxBurstSize);
        if (SL_EXPECT_FALSE(pulled == 0)) return;

        for (std::size_t i = 0; i < pulled; ++i) {
            void* addr = nullptr;
            if constexpr (std::is_pointer_v<typename TargetConduit::value_type>) {
                addr = static_cast<void*>(batch[i]);
            } else {
                addr = batch[i].get_address();
            }

            // on
            size_t send_size = 0;
            const char* send_buf = nullptr;
            using FrameType = typename TargetConduit::value_type_pod;

            if constexpr (requires { static_cast<transport::raw_tcp_frame*>(addr)->data; }) {
                auto* raw = static_cast<transport::raw_tcp_frame*>(addr);
                send_buf = reinterpret_cast<const char*>(raw->data);
                send_size = raw->payload_length;
            } else {
                send_buf = static_cast<const char*>(addr);
                send_size = sizeof(FrameType);
            }

            // 2. KERNEL HANDOFF (Standard Syscall Fallback)
            ssize_t res = ::send(socket_fd_, send_buf, send_size, MSG_NOSIGNAL);

            if (SL_EXPECT_TRUE(res >= 0)) {
                // 3. RECLAMATION: Only release on success
                mem_pool_.release(reinterpret_cast<FrameType*>(addr));
                tx_count_++;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Backpressure: Return failed and remaining frames to conduit
                interconnect_conduit_.revert_batch(batch + i, pulled - i);
                return;
            }
        }
    }
};

}  // namespace slabflux::core
