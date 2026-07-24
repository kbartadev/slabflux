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

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"
#include "slabflux/net/virtual_tcp_socket.hpp"
#include "slabflux/net/tcp_transmission_control_block.hpp"
#include "slabflux/net/network_conduit.hpp" // For wire_frame and wire_protocol

namespace slabflux::net {

    template <typename EventType, size_t TxCapacity = 1024, size_t RxBufferSize = 65536>
    class alignas(64) network_conduit_virtual {
    private:
        virtual_tcp_socket* socket_{nullptr};
        
        // TX lock-free ring for buffered L7 events pending serialization
        core::spsc_ring_conduit<wire_frame<EventType>, TxCapacity> tx_ring_;
        
        // RX contiguous staging buffer for partial read deserialization
        alignas(64) char rx_buffer_[RxBufferSize];
        size_t rx_cursor_{0};
        size_t tx_inflight_offset_{0}; // Protects against partial-send duplication

        bool dead_{true};

    public:
        network_conduit_virtual() noexcept = default;

        SLAB_HOT void bind(virtual_tcp_socket* sock) noexcept {
            socket_ = sock;
            dead_ = false;
            rx_cursor_ = 0;
        }

        /**
         * @brief Initiates an Active Open (Client Handshake)
         */
        SLAB_HOT void open(uint32_t target_ip, uint16_t target_port) noexcept {
            if (SL_EXPECT_TRUE(socket_)) {
                socket_->connect(target_ip, target_port);
                dead_ = false;
            }
        }

        SLAB_HOT bool push(const EventType& ev) noexcept {
            if (SL_EXPECT_FALSE(dead_ || !socket_)) return false;
            auto* slot = tx_ring_.get_reserved_slot(0);
            if (SL_EXPECT_TRUE(slot != nullptr)) {
                wire_protocol<EventType>::serialize(*slot, ev);
                tx_ring_.commit_n(1);
                return true;
            }
            return false; // Backpressure: Ring is full
        }

        SLAB_HOT void poll_tx() noexcept {
            if (SL_EXPECT_FALSE(dead_ || !socket_)) return;
            if (SL_EXPECT_FALSE(!socket_->is_established())) return; // Await SYN-ACK

            while (tx_ring_.available_to_peek() > 0) {
                const wire_frame<EventType>* frame = tx_ring_.get_peek_slot(0);
                const char* bytes = reinterpret_cast<const char*>(frame) + tx_inflight_offset_;
                size_t len = sizeof(wire_frame<EventType>) - tx_inflight_offset_;

                // Sends directly to the TCP Fragmenter inside the virtual socket
                ssize_t sent = socket_->send(bytes, len);
                if (SL_EXPECT_TRUE(sent == static_cast<ssize_t>(len))) {
                    tx_inflight_offset_ = 0;
                    tx_ring_.consume_n(1);
                } else {
                    if (sent < 0) mark_dead(); // Connection forcibly terminated
                    else tx_inflight_offset_ += sent; // Buffer backpressure hit, yield safely
                    break; // sent == 0 maps to EWOULDBLOCK. Yield and wait for Window Update.
                }
            }
        }

        template <typename Sink>
        SLAB_HOT void poll_rx(Sink& sink) noexcept {
            if (SL_EXPECT_FALSE(dead_ || !socket_ || !socket_->is_established())) return;

            ssize_t read_bytes = socket_->recv(rx_buffer_ + rx_cursor_, RxBufferSize - rx_cursor_);
            if (SL_EXPECT_TRUE(read_bytes > 0)) {
                rx_cursor_ += read_bytes;
                
                size_t events_ready = rx_cursor_ / sizeof(wire_frame<EventType>);
                for (size_t i = 0; i < events_ready; ++i) {
                    auto* frame = reinterpret_cast<wire_frame<EventType>*>(rx_buffer_ + (i * sizeof(wire_frame<EventType>)));
                    if (SL_EXPECT_FALSE(frame->type_id != EventType::TYPE_ID)) {
                        mark_dead();
                        return;
                    }
                    EventType ev;
                    wire_protocol<EventType>::deserialize(*frame, ev);
                    if constexpr (requires { sink.process(ev); }) {
                        sink.process(ev);
                    } else if constexpr (requires { sink.on_event(ev); }) {
                        sink.on_event(ev);
                    } else {
                        sink.on(ev);
                    }
                }
                
                size_t consumed = events_ready * sizeof(wire_frame<EventType>);
                if (consumed > 0 && consumed < rx_cursor_) std::memmove(rx_buffer_, rx_buffer_ + consumed, rx_cursor_ - consumed);
                rx_cursor_ -= consumed;
            } else if (read_bytes == 0) {
                mark_dead(); // EOF / Teardown
            }
        }

        SLAB_FORCE_INLINE void mark_dead() noexcept { dead_ = true; if (socket_) socket_->close(); }
        SLAB_FORCE_INLINE bool is_dead() const noexcept { return dead_; }
    };
}