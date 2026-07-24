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
 * ============================================================================* @file virtual_tcp_socket.hpp
 * @brief High-Level OS-Bypass TCP API mapped to internal TCB structures.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "slabflux/net/tcp_flow_engine.hpp"
#include "slabflux/transport/tcp_stream_fragmenter.hpp"
#include "slabflux/net/tcp_transmission_control_block.hpp"
#include "slabflux/net/tcp_wire_engine.hpp"
#include <string_view>
#include <algorithm>

// Forward declaration of assumed existing fragmenter API defined in system architecture bounds
namespace slabflux::net {
    struct tcp_stream_fragmenter {
        static void transmit(tcp_transmission_control_block* tcb, std::string_view payload) noexcept;
    };
}

namespace slabflux::net {

    /**
     * @brief Zero-Allocation Virtual Socket.
     * @details Mimics POSIX socket semantics for BusinessLogic usage, but interacts
     * exclusively with SlabFlux internal memory rings.
     */
    // 100% User-Space API Façade for Application Layer Logic (Zero Syscalls)
    class virtual_tcp_socket {
    private:
        tcp_transmission_control_block* tcb_{nullptr};
        uint16_t mss_{1460}; // Maximum Segment Size

        struct {
            uint32_t lifetime{0};
            uint32_t age_add{0};
            std::string_view ticket{};
        } pending_psk_;

    public:
        explicit virtual_tcp_socket(tcp_transmission_control_block* tcb) noexcept : tcb_(tcb) {}

        [[nodiscard]] SLAB_FORCE_INLINE bool is_valid() const noexcept {
            return tcb_ != nullptr;
        }

        SLAB_FORCE_INLINE void get_remote_ipv6(uint64_t out_ip[2]) const noexcept {
            if (tcb_ && tcb_->is_ipv6) {
                out_ip[0] = tcb_->remote_ipv6[0]; out_ip[1] = tcb_->remote_ipv6[1];
            } else { out_ip[0] = 0; out_ip[1] = 0; }
        }

        SLAB_HOT bool connect_ipv6(const uint64_t target_ip[2], uint16_t target_port) noexcept {
            if (SL_EXPECT_FALSE(!tcb_ || tcb_->phase_mask != PHASE_CLOSED)) return false;
            if (SL_EXPECT_FALSE(!tcb_->tx_egress_conduit)) return false;
            
            auto* egress = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb_->tx_egress_conduit);
            outbound_tcp_segment<1460>* slot = egress->get_reserved_slot(0);
            if (SL_EXPECT_FALSE(!slot)) return false;
            
            if (SL_EXPECT_FALSE(!tcb_->tx_mbuf_pool)) return false;
            slot->mbuf = rte_pktmbuf_alloc(tcb_->tx_mbuf_pool);
            if (SL_EXPECT_FALSE(!slot->mbuf)) return false;
            
            tcb_->phase_mask = PHASE_SYN_SENT;
            tcb_->remote_ipv6[0] = target_ip[0];
            tcb_->remote_ipv6[1] = target_ip[1];
            tcb_->remote_port = target_port;
            tcb_->is_ipv6 = 1;
            
            bool mac_resolved = false;
            for (int i = 0; i < 6; ++i) if (tcb_->target_mac[i] != 0) mac_resolved = true;
            if (!mac_resolved) {
                tcb_->temporal_flags |= TEMP_FLAG_ARP_WAIT;
                slot->release();
                return false; // Connection pending NDP broadcast
            }

            tcp_flow_engine::prepare_outbound(*tcb_, *slot, 0, FLAG_SYN);
            egress->commit_n(1);
            return true;
        }

        /**
         * @brief Validates if the socket is capable of bidirectional or half-open transmission.
         */
        [[nodiscard]] SLAB_FORCE_INLINE bool is_established() const noexcept {
            return is_valid() && (tcb_->phase_mask & (PHASE_ESTABLISHED | PHASE_CLOSE_WAIT));
        }

        [[nodiscard]] SLAB_FORCE_INLINE bool can_receive() const noexcept {
            return is_valid() && (tcb_->phase_mask & (PHASE_ESTABLISHED | PHASE_FIN_WAIT1 | PHASE_FIN_WAIT2));
        }

        [[nodiscard]] SLAB_FORCE_INLINE uint32_t get_remote_ipv4() const noexcept {
            return tcb_ ? tcb_->remote_ipv4 : 0;
        }

        /**
         * @brief Proactively caches a 0-RTT PSK session ticket for the target connection.
         * @details Temporarily holds the ticket at the L4 boundary. The L7 TLS Record Layer 
         * intercepts this during handshake ignition and moves it to the cryptographic vault.
         */
        SLAB_HOT void configure_tls_session_ticket(uint32_t lifetime, uint32_t age_add, std::string_view ticket) noexcept {
            pending_psk_.lifetime = lifetime;
            pending_psk_.age_add = age_add;
            pending_psk_.ticket = ticket;
        }

        [[nodiscard]] SLAB_FORCE_INLINE auto get_pending_psk() const noexcept { return pending_psk_; }
        SLAB_FORCE_INLINE void clear_pending_psk() noexcept { pending_psk_.ticket = {}; }

        /**
         * @brief Natively configures TCP Keep-Alive polling for this connection.
         */
        SLAB_HOT void set_keepalive(bool enable) noexcept {
            if (SL_EXPECT_FALSE(!is_valid())) return;
            if (enable) {
                tcb_->temporal_flags |= TEMP_FLAG_KEEPALIVE;
            } else {
                tcb_->temporal_flags &= ~TEMP_FLAG_KEEPALIVE;
            }
        }

        /**
         * @brief Active Open (Connect).
         * @details Injects a SYN frame into the outbound conduit and transitions state.
         */
        SLAB_HOT bool connect(uint32_t target_ip, uint16_t target_port) noexcept {
            if (SL_EXPECT_FALSE(!tcb_ || tcb_->phase_mask != PHASE_CLOSED)) return false;
            if (SL_EXPECT_FALSE(!tcb_->tx_egress_conduit)) return false;
            
            auto* egress = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb_->tx_egress_conduit);
            outbound_tcp_segment<1460>* slot = egress->get_reserved_slot(0);
            if (SL_EXPECT_FALSE(!slot)) return false;
            
            if (SL_EXPECT_FALSE(!tcb_->tx_mbuf_pool)) return false;
            slot->mbuf = rte_pktmbuf_alloc(tcb_->tx_mbuf_pool);
            if (SL_EXPECT_FALSE(!slot->mbuf)) return false;
            
            tcb_->phase_mask = PHASE_SYN_SENT;
            tcb_->remote_ipv4 = target_ip;
            tcb_->remote_port = target_port;
            
            // ARP Resolution Check: If MAC is all zeros, set ARP wait flag.
            bool mac_resolved = false;
            for (int i = 0; i < 6; ++i) if (tcb_->target_mac[i] != 0) mac_resolved = true;
            if (!mac_resolved) {
                tcb_->temporal_flags |= TEMP_FLAG_ARP_WAIT;
                slot->release();
                return false; // Connection pending ARP broadcast
            }

            tcp_flow_engine::prepare_outbound(*tcb_, *slot, 0, FLAG_SYN);
            egress->commit_n(1);
            return true;
        }

        /**
         * @brief Non-blocking Send.
         */
        SLAB_HOT ssize_t send(const char* buffer, size_t length) noexcept {
            if (SL_EXPECT_FALSE(!is_established())) return -1;
            
            uint32_t inflight = tcb_->snd_nxt - tcb_->snd_una;
            uint32_t rwnd_limit = (tcb_->snd_wnd > inflight) ? (tcb_->snd_wnd - inflight) : 0;
            uint32_t cwnd_limit = (tcb_->cwnd > inflight) ? (tcb_->cwnd - inflight) : 0;
            uint32_t usable_window = std::min(cwnd_limit, rwnd_limit);
            
            // Adhere to the peer's physical capability via MTU discovery negotiation
            uint16_t current_mss = (tcb_->remote_mss > 0 && tcb_->remote_mss < mss_) ? tcb_->remote_mss : mss_;

            // MTU Violation Fix: Deduct anticipated TCP Options from MSS
            uint8_t opt_len = 0;
            if (tcb_->ts_permitted) opt_len += 12;
            uint8_t num_sack_blocks = 0;
            if (tcb_->sack_permitted) {
                for (int i = 0; i < 3; ++i) if (tcb_->sack_valid[i]) num_sack_blocks++;
            }
            // CRITICAL FIX: SACK Options consume 4 bytes of header (NOP, NOP, Kind, Len) + Blocks
            if (num_sack_blocks > 0) opt_len += 4 + (num_sack_blocks * 8);
            if (opt_len % 4 != 0) opt_len += (4 - (opt_len % 4));
            uint16_t effective_mss = (current_mss > opt_len) ? current_mss - opt_len : 536;

            // Sender-Side Silly Window Syndrome (SWS) Avoidance Guard
            // Bypassed if inflight == 0 to prevent network deadlock
            if (SL_EXPECT_FALSE(usable_window > 0 && usable_window < effective_mss && usable_window < (tcb_->snd_wnd / 2))) {
                if (inflight > 0 && length > usable_window) return 0; // Wait for the window to open significantly
            }

            size_t to_send = std::min(length, static_cast<size_t>(usable_window));
            if (to_send == 0) {
                // Application wants to send but window is closed. Engage Zero-Window Probing.
                if (length > 0 && tcb_->snd_wnd == 0) tcb_->temporal_flags |= TEMP_FLAG_ZWP_ACTIVE;
                return 0; // EWOULDBLOCK mapping
            }
            
            auto* egress = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb_->tx_egress_conduit);
            auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb_->tx_unacked_ring);
            
            if (to_send > 0) {
                tcb_->temporal_flags &= ~TEMP_FLAG_ACK_PENDING;
                tcb_->delayed_ack_deadline_tsc = 0;
            }

            size_t bytes_sent = 0;
            while (bytes_sent < to_send) {
                uint32_t chunk = static_cast<uint32_t>(std::min(static_cast<size_t>(effective_mss), to_send - bytes_sent));
                
                outbound_tcp_segment<1460>* slot = egress->get_reserved_slot(0);
                outbound_tcp_segment<1460>* unacked_slot = unacked ? unacked->get_reserved_slot(0) : nullptr;
                if (SL_EXPECT_FALSE(!slot || (unacked && !unacked_slot))) break; // Native hardware backpressure
                
                slot->mbuf = rte_pktmbuf_alloc(tcb_->tx_mbuf_pool);
                if (SL_EXPECT_FALSE(!slot->mbuf)) break; // Mempool exhausted!

                
                // Configure TCP Boundaries FIRST so get_payload() evaluates correctly
                tcp_flow_engine::prepare_outbound(*tcb_, *slot, chunk, FLAG_PSH);
                
                // Write the data into the correctly shifted Mbuf Offset
                std::memcpy(slot->get_payload(tcb_->is_ipv6), buffer + bytes_sent, chunk);

                // Hardware-Agnostic TX Checksum Fallback
                if (tcb_->is_ipv6) {
                    auto* hdr6 = reinterpret_cast<raw_tcp_ipv6_frame*>(slot->get_header());
                    uint16_t tcp_len = chunk + (hdr6->tcp_data_offset >> 4) * 4;
                    uint32_t pseudo_sum = 0;
                    const uint16_t* src16 = reinterpret_cast<const uint16_t*>(hdr6->ipv6_src);
                    const uint16_t* dst16 = reinterpret_cast<const uint16_t*>(hdr6->ipv6_dst);
                    for(int i=0; i<8; ++i) pseudo_sum += src16[i];
                    for(int i=0; i<8; ++i) pseudo_sum += dst16[i];
                    pseudo_sum += core::endian::host_to_network16(6);
                    pseudo_sum += core::endian::host_to_network16(tcp_len);
                    hdr6->tcp_checksum = 0;
                    hdr6->tcp_checksum = tcp_wire_engine::compute_checksum(&hdr6->tcp_src_port, tcp_len, pseudo_sum);
                } else {
                    auto* hdr = slot->get_header();
                    uint16_t tcp_len = chunk + (hdr->tcp_data_offset >> 4) * 4;
                    uint32_t pseudo_sum = 0;
                    uint32_t src = hdr->ip_src;
                    uint32_t dst = hdr->ip_dst;
                    pseudo_sum += (src & 0xFFFF) + (src >> 16);
                    pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
                    pseudo_sum += core::endian::host_to_network16(6);
                    pseudo_sum += core::endian::host_to_network16(tcp_len);
                    hdr->tcp_checksum = 0;
                    hdr->tcp_checksum = tcp_wire_engine::compute_checksum(&hdr->tcp_src_port, tcp_len, pseudo_sum);
                }

                if (unacked_slot) {
                    unacked_slot->mbuf = slot->mbuf;
                    unacked_slot->retain();
                    unacked->commit_n(1);
                }
                
                egress->commit_n(1); // Uncage directly onto the DPDK / io_uring wire
                bytes_sent += chunk;
            }
            return bytes_sent == 0 ? 0 : static_cast<ssize_t>(bytes_sent);
        }

        /**
         * @brief Non-blocking Recv.
         * @details Direct stream buffer extraction for OS socket parity.
         */
        SLAB_HOT ssize_t recv(char* buffer, size_t max_length) noexcept {
            if (SL_EXPECT_FALSE(!tcb_->rx_stream_ring)) return -1;
            auto* rx_ring = static_cast<core::spsc_ring_conduit<char, 4096>*>(tcb_->rx_stream_ring);
            size_t available = rx_ring->available_to_peek();
            
            if (available == 0) {
                // POSIX Parity: Return 0 strictly if the peer closed the connection and the ring is empty.
                if (!can_receive()) return 0; // EOF
                return -1; // EWOULDBLOCK
            }

            size_t capacity = 4096;
            uint32_t before_free = static_cast<uint32_t>(capacity - available);
            size_t to_read = (max_length < available) ? max_length : available;
            
            for (size_t i = 0; i < to_read; ++i) {
                buffer[i] = *rx_ring->get_peek_slot(i);
            }
            rx_ring->consume_n(to_read);
            
            // Receiver-Side Window Update Generation (Flow Control Anti-Deadlock)
            uint32_t after_free = before_free + static_cast<uint32_t>(to_read);
            uint32_t mss = (tcb_->remote_mss > 0) ? tcb_->remote_mss : 1460;
            if (before_free < mss && before_free < (capacity / 2)) {
                if (after_free >= mss || after_free >= (capacity / 2)) {
                    tcb_->flags_pending |= FLAG_ACK;
                    tcp_flow_engine::dispatch_control_frames(*tcb_);
                }
            }

            return static_cast<ssize_t>(to_read);
        }

        /**
         * @brief Initiates graceful TCP teardown.
         */
        inline void close() noexcept {
            if (is_valid()) {
                if (tcb_->phase_mask & (PHASE_ESTABLISHED | PHASE_CLOSE_WAIT)) {
                    bool unread_data = false;
                    if (tcb_->rx_stream_ring) {
                        auto* rx_ring = static_cast<core::spsc_ring_conduit<char, 4096>*>(tcb_->rx_stream_ring);
                        if (rx_ring->available_to_peek() > 0) unread_data = true;
                    }
                    
                    if (SL_EXPECT_FALSE(unread_data)) {
                        // RFC 9293 Orphaned RX Data: MUST send RST if data remains unread upon close
                        tcb_->phase_mask = PHASE_CLOSED;
                        tcb_->flags_pending |= FLAG_RST;
                    } else {
                        tcb_->phase_mask = (tcb_->phase_mask & PHASE_ESTABLISHED) ? PHASE_FIN_WAIT1 : PHASE_LAST_ACK;
                        tcb_->flags_pending |= FLAG_FIN; // Schedule the physical FIN frame for immediate native transmit
                    }
                    tcp_flow_engine::dispatch_control_frames(*tcb_); // Eliminate teardown jitter
                } else if (tcb_->phase_mask & (PHASE_SYN_SENT | PHASE_SYN_RCVD)) {
                    // Application cancelled the connection prior to handshake completion
                    tcb_->phase_mask = PHASE_CLOSED;
                    tcb_->flags_pending |= FLAG_RST;
                    tcp_flow_engine::dispatch_control_frames(*tcb_);
                }
            }
        }
    };
} // namespace slabflux::net