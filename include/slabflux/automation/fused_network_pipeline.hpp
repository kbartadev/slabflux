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
#include <atomic>
#include <thread>
#include <cstddef>
#include <sys/socket.h>
#include <errno.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#include "slabflux/core.hpp"
#include "slabflux/transport/baremetal_parser.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/core/hardware_topology.hpp"

namespace slabflux::automation {

    /**
     * @brief Compile-time static protocol identification layout.
     */
    struct flat_binary_parser {
        struct parsing_state {
            std::size_t total_bytes_consumed{0};
            void reset() noexcept { total_bytes_consumed = 0; }
        };

        template <typename TargetFrame>
        inline static transport::parser_status parse(
            std::string_view buffer,
            parsing_state& state
        ) noexcept {
            if (buffer.size() < sizeof(TargetFrame)) {
                state.total_bytes_consumed = 0;
                return transport::parser_status::INCOMPLETE;
            }
            state.total_bytes_consumed = sizeof(TargetFrame);
            return transport::parser_status::OK;
        }
    };

    /**
     * @brief High-Velocity Network Processing Grid Driver.
     * @details Manages hardware-pinned OS thread lifecycles and drives library execution lanes.
     * @tparam Stage1Lane Type-deduced core::pipeline_lane matching L4/L5 ingress defragmentation.
     * @tparam Stage1Route Policy trait handling data ingestion rules for Stage 1.
     * @tparam Stage2Lane Type-deduced core::pipeline_lane matching downstream business processing.
     * @tparam Stage2Route Policy trait handling data routing rules for Stage 2.
     * @tparam VectorizedStream Mode switch enabling pure SIMD vector streaming execution.
     */
    template <
        typename Stage1Lane,
        typename Stage1Route,
        typename Stage2Lane,
        typename Stage2Route,
        bool VectorizedStream = true
    >
    class fused_network_pipeline {
    private:
        int socket_fd_;
        std::atomic<bool> running_{true};

        std::thread ingress_thread_;
        std::thread stage1_thread_;
        std::thread stage2_thread_;

        // Shared topology routing contexts
        Stage1Route& stage1_route_;
        Stage2Route& stage2_route_;

        // Unified compute cells
        Stage1Lane& stage1_lane_;
        Stage2Lane& stage2_lane_;

    public:
        explicit fused_network_pipeline(
            int socket_fd,
            Stage1Lane& s1_lane, Stage1Route& s1_route,
            Stage2Lane& s2_lane, Stage2Route& s2_route
        ) noexcept
            : socket_fd_(socket_fd)
            , stage1_route_(s1_route)
            , stage2_route_(s2_route)
            , stage1_lane_(s1_lane)
            , stage2_lane_(s2_lane) 
        {
            start_hardware_grid();
        }

        ~fused_network_pipeline() noexcept {
            running_.store(false, std::memory_order_relaxed);
            if (ingress_thread_.joinable()) ingress_thread_.join();
            if (stage1_thread_.joinable()) stage1_thread_.join();
            if (stage2_thread_.joinable()) stage2_thread_.join();
        }

        fused_network_pipeline(const fused_network_pipeline&) = delete;
        fused_network_pipeline& operator=(const fused_network_pipeline&) = delete;

    private:
        void start_hardware_grid() noexcept {

            // ============================================================================
            // STAGE 2 WORKER: Downstream Business Execution Node (Core 3)
            // ============================================================================
            stage2_thread_ = std::thread([this]() noexcept {
                core::hardware_topology::pin_thread(3);

                while (SL_EXPECT_TRUE(running_.load(std::memory_order_relaxed))) {
                    bool worked;
                    if constexpr (VectorizedStream) {
                        worked = stage2_lane_.execute_vector_stream(stage2_route_);
                    } else {
                        worked = stage2_lane_.execute_tick(stage2_route_);
                    }

                    if (!worked) {
                        #if defined(__x86_64__) || defined(_M_X64)
                        _mm_pause();
                        #endif
                    }
                }
            });

            // ============================================================================
            // STAGE 1 WORKER: L4/L5 Transport Parsing Node (Core 2)
            // ============================================================================
            stage1_thread_ = std::thread([this]() noexcept {
                core::hardware_topology::pin_thread(2);

                while (SL_EXPECT_TRUE(running_.load(std::memory_order_relaxed))) {
                    bool worked;
                    if constexpr (VectorizedStream) {
                        worked = stage1_lane_.execute_vector_stream(stage1_route_);
                    } else {
                        worked = stage1_lane_.execute_tick(stage1_route_);
                    }

                    if (!worked) {
                        #if defined(__x86_64__) || defined(_M_X64)
                        _mm_pause();
                        #endif
                    }
                }
            });

            // ============================================================================
            // INGRESS ENGINE: Direct Bare-Metal Socket Polling Node (Core 1)
            // ============================================================================
            ingress_thread_ = std::thread([this]() noexcept {
                core::hardware_topology::pin_thread(1);

                // Extract target entrance handle from the routing trait context
                auto& network_target_route = stage1_route_;

                while (SL_EXPECT_TRUE(running_.load(std::memory_order_relaxed))) {
                    // FUSED NEXUS REPAIR: Pull a unique frame from the Slab Matrix for every packet.
                    // Never use a single stack-local address for cross-thread handoff.
                    auto* frame = stage1_lane_.get_pool().make_raw();
                    if (SL_EXPECT_FALSE(!frame)) continue;

                    // Prevent SIGPIPE process termination on remote TCP RST
                    ssize_t n = ::recv(socket_fd_, frame->data, 1460, MSG_NOSIGNAL);

                    if (__builtin_expect(n <= 0, 0)) {
                        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                            #if defined(__x86_64__) || defined(_M_X64)
                            _mm_pause();
                            #endif
                            continue;
                        }
                        stage1_lane_.get_pool().release(frame);
                        break;
                    }

                    frame->payload_length = static_cast<uint16_t>(n);
                    frame->connection_id  = static_cast<uint32_t>(socket_fd_);

                    core::tagged_pointer token =
                        core::tagged_pointer::pack(transport::raw_tcp_frame::ID, frame);

                    // Route token down to the matrix. If a sharded topology is used,
                    // backpressure or overflow logic is implemented here.
                    while (!network_target_route.push_to_matrix(token)) {
                        #if defined(__x86_64__) || defined(_M_X64)
                        _mm_pause();
                        #endif
                    }
                }
            });
        }
    };

} // namespace slabflux::automation
