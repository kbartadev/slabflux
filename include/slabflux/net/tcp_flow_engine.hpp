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
 * ============================================================================* @file tcp_flow_engine.hpp
 * @brief SlabFlux-invariant TCP Protocol State Tracking.
 */

#pragma once

#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"
#include "slabflux/net/tcp_transmission_control_block.hpp"
#include "slabflux/net/tcp_spatial_ooo_matrix.hpp"
#include "slabflux/net/raw_tcp_ipv4_frame.hpp"
#include "slabflux/net/raw_tcp_ipv6_frame.hpp"
#include "slabflux/core/endian.hpp"
#include <rte_mbuf.h>
#include <rte_ip.h>
#include "slabflux/net/tcp_wire_engine.hpp"
#include "slabflux/net/tcp_syn_cookie.hpp"
#include <iostream>

#ifndef SLAB_FLOW_DEBUG
#define SLAB_FLOW_DEBUG(msg) std::cout << "[FLOW DEBUG " << __LINE__ << "] " << msg << std::endl
#define FLOW_DROP_FALSE(msg) do { SLAB_FLOW_DEBUG(msg); return false; } while(0)
#define FLOW_DROP_TRUE(msg)  do { SLAB_FLOW_DEBUG(msg); return true; } while(0)
#endif

namespace slabflux::net {

    enum tcp_flags : uint8_t {
        FLAG_FIN = 0x01, FLAG_SYN = 0x02, FLAG_RST = 0x04,
        FLAG_PSH = 0x08, FLAG_ACK = 0x10, FLAG_URG = 0x20,
        FLAG_ECE = 0x40, FLAG_CWR = 0x80
    };

    /**
     * @brief Structural fusion of Header + Payload.
     * @details Guarantees contiguous cache residency without heap allocation.
     */
    template <size_t MSS = 1460>
    struct alignas(core::CACHE_LINE_SIZE) outbound_tcp_segment {
        struct rte_mbuf* mbuf{nullptr};
        bool is_sacked{false};

        SLAB_FORCE_INLINE raw_tcp_ipv4_frame* get_header() const noexcept {
            return rte_pktmbuf_mtod(mbuf, raw_tcp_ipv4_frame*);
        }

        SLAB_FORCE_INLINE char* get_payload(bool is_ipv6 = false) const noexcept {
            if (is_ipv6) {
                auto* hdr6 = reinterpret_cast<raw_tcp_ipv6_frame*>(get_header());
                uint8_t data_offset_words = hdr6->tcp_data_offset >> 4;
                return rte_pktmbuf_mtod_offset(mbuf, char*, 54 + (data_offset_words * 4));
            } else {
                uint8_t data_offset_words = get_header()->tcp_data_offset >> 4;
                return rte_pktmbuf_mtod_offset(mbuf, char*, 34 + (data_offset_words * 4));
            }
        }

        SLAB_FORCE_INLINE uint32_t get_payload_length(bool is_ipv6 = false) const noexcept {
            if (is_ipv6) {
                auto* hdr6 = reinterpret_cast<const raw_tcp_ipv6_frame*>(get_header());
                uint16_t ipv6_plen = core::endian::network_to_host16(hdr6->ipv6_plen);
                uint8_t data_offset_words = hdr6->tcp_data_offset >> 4;
                uint32_t tcp_header_len = (data_offset_words * 4);
                return ipv6_plen > tcp_header_len ? ipv6_plen - tcp_header_len : 0;
            } else {
                uint16_t ip_len = core::endian::network_to_host16(get_header()->ip_len);
                uint8_t ip_hlen = (get_header()->ip_ihl_ver & 0x0F) * 4;
                uint8_t data_offset_words = get_header()->tcp_data_offset >> 4;
                uint32_t headers = ip_hlen + (data_offset_words * 4);
                return ip_len > headers ? ip_len - headers : 0;
            }
        }

        SLAB_FORCE_INLINE void set_payload_length(uint32_t len, bool is_ipv6 = false) noexcept {
            uint32_t header_len;
            if (is_ipv6) {
                auto* hdr6 = reinterpret_cast<raw_tcp_ipv6_frame*>(get_header());
                uint8_t data_offset_words = hdr6->tcp_data_offset >> 4;
                header_len = 54 + (data_offset_words * 4);
            } else {
                uint8_t data_offset_words = get_header()->tcp_data_offset >> 4;
                header_len = 34 + (data_offset_words * 4);
            }
            
            // CRITICAL FIX: Axiom 9 - Asymmetric Structural Truncation via MBUF Volumetric Wrap
            if (SL_EXPECT_FALSE(len > 65535 - header_len)) len = 65535 - header_len;
            uint32_t total_len = header_len + len;
            // CRITICAL FIX: Etherleak Information Exposure (CVE-2003-0001)
            if (SL_EXPECT_FALSE(total_len < 60)) {
                // Since max padding is 6 bytes (60 - 54), a single 64-bit store 
                // zeros the space instantly without a libc call.
                *reinterpret_cast<uint64_t*>(reinterpret_cast<char*>(get_header()) + total_len) = 0;
                total_len = 60;
            }
            mbuf->data_len = total_len;
            mbuf->pkt_len = total_len;
        }

        SLAB_FORCE_INLINE bool retain() const noexcept {
            if (mbuf) {
                // CRITICAL FIX: Axiom 2 - Asymmetric Substrate Rupture via Reference Counter Saturation
                if (SL_EXPECT_FALSE(rte_mbuf_refcnt_read(mbuf) >= 0xFF00)) return false;
                rte_pktmbuf_refcnt_update(mbuf, 1);
            }
            return true;
        }

        SLAB_FORCE_INLINE void release() noexcept {
            if (mbuf) {
                rte_pktmbuf_free(mbuf);
                mbuf = nullptr;
            }
        }
    };

    class tcp_flow_engine {
    public:
        static SLAB_FORCE_INLINE void trigger_challenge_ack(tcp_transmission_control_block& tcb) noexcept {
            uint64_t now = __rdtsc();
            // CRITICAL FIX: Axiom 35 - Asymptotic Saturation of the Challenge-Response Boundary
            if (now - tcb.last_challenge_tsc > 3000000000ULL) { // ~1 second hysteresis
                tcb.challenge_ack_cnt = 0;
                tcb.last_challenge_tsc = now;
            }
            if (SL_EXPECT_TRUE(tcb.challenge_ack_cnt < 2)) {
                tcb.challenge_ack_cnt++;
                tcb.flags_pending |= FLAG_ACK;
            }
        }

        // Compatibility overload for older tests without trim_offset
        template <typename FrameType>
        static SLAB_HOT bool process_inbound(tcp_transmission_control_block& tcb, const FrameType& hdr, uint32_t& payload_len, struct rte_mbuf* mbuf = nullptr, uint16_t payload_offset = 0, bool* mbuf_absorbed = nullptr, uint32_t conn_id = 0) noexcept {
            uint32_t dummy_trim = 0;
            return process_inbound(tcb, hdr, payload_len, dummy_trim, mbuf, payload_offset, mbuf_absorbed, conn_id);
        }

        /**
         * @brief Branchless State Transition Evaluation.
         * @return True if the frame is accepted, False if dropped (out of window / bad state).
         */
        template <typename FrameType>
        static SLAB_HOT bool process_inbound(tcp_transmission_control_block& tcb, const FrameType& hdr, uint32_t& payload_len, uint32_t& trim_offset, struct rte_mbuf* mbuf = nullptr, uint16_t payload_offset = 0, bool* mbuf_absorbed = nullptr, uint32_t conn_id = 0) noexcept {
            uint32_t host_seq = core::endian::network_to_host32(hdr.tcp_seq);
            uint32_t host_ack = core::endian::network_to_host32(hdr.tcp_ack);
            uint16_t host_wnd = core::endian::network_to_host16(hdr.tcp_window);
            uint32_t original_payload_len = payload_len;

            // Mathematical sequence validation (Branchless bounding)
            // Safely validates 32-bit wrap-arounds and partially overlapping retransmissions
            uint32_t end_seq = host_seq + original_payload_len;
            bool seq_valid = false;
            if (SL_EXPECT_FALSE(tcb.phase_mask == PHASE_SYN_SENT)) {
                // Active Open (Client Mode): Accept the SYN-ACK if the ACK acknowledges our ISN
                if (hdr.tcp_flags & FLAG_ACK) {
                    if (host_ack == tcb.snd_nxt) seq_valid = true;
                } else if (hdr.tcp_flags & FLAG_SYN) {
                    seq_valid = true; // Simultaneous Open
                }
            } else if (tcb.rcv_wnd == 0) {
                seq_valid = (original_payload_len == 0) && (host_seq == tcb.rcv_nxt);
            } else {
                // CRITICAL FIX: Axiom 30 - Monoidal Asymmetry in Sequence Wrap Overlap
                int32_t dist_head = static_cast<int32_t>(host_seq - tcb.rcv_nxt);
                int32_t dist_tail = static_cast<int32_t>(end_seq - 1 - tcb.rcv_nxt);
                
                seq_valid = (dist_head >= 0 && static_cast<uint32_t>(dist_head) < tcb.rcv_wnd) || 
                            (original_payload_len > 0 && dist_tail >= 0 && static_cast<uint32_t>(dist_tail) < tcb.rcv_wnd);
            }

            if (SL_EXPECT_FALSE(!seq_valid)) {
                if (hdr.tcp_flags & FLAG_RST) {
                    return false; // RFC 5961: Out of window RSTs MUST be dropped silently.
                }
                // CRITICAL FIX: RFC 793 Bad ACK in SYN-SENT requires immediate RST
                // Sending a Challenge ACK here violates the RFC and triggers an infinite packet storm.
                if (SL_EXPECT_FALSE(tcb.phase_mask == PHASE_SYN_SENT && (hdr.tcp_flags & FLAG_ACK))) {
                    tcb.flags_pending |= FLAG_RST;
                    payload_len = 0;
                    return true;
                }
                // ACK Storm Prevention
                // Silently drop out-of-window pure ACKs to break ping-pong desynchronization loops.
                if (original_payload_len == 0 && hdr.tcp_flags == FLAG_ACK) {
                    // CRITICAL FIX: Axiom 15 - Gauge Field Inversion in Temporal Probes
                    // Safely respond to Keep-Alive probes natively handling 32-bit wrap underflows
                    if (static_cast<int32_t>(tcb.rcv_nxt - host_seq) == 1) {
                        trigger_challenge_ack(tcb);
                        payload_len = 0;
                        return true;
                    }
                    return false;
                }
                // RFC 9293: Out of window segments MUST elicit an ACK to resynchronize the peer.
                trigger_challenge_ack(tcb);
                payload_len = 0;
                return true; 
            }

            // GFW / DPI Middlebox Defense: TTL Hop-Count Validation
            // If a valid sequence packet arrives with a drastically shifted TTL, it is a blind injection.
            if (SL_EXPECT_TRUE(tcb.expected_ttl != 0)) {
                uint8_t ip_ttl_val;
                if constexpr (std::is_same_v<FrameType, raw_tcp_ipv4_frame>) ip_ttl_val = hdr.ip_ttl;
                else ip_ttl_val = hdr.ipv6_hlim;

                int ttl_diff = static_cast<int>(ip_ttl_val) - static_cast<int>(tcb.expected_ttl);
                if (SL_EXPECT_FALSE(ttl_diff < -6 || ttl_diff > 6)) {
                    return false; // Silently drop GFW/Middlebox spoofed packet
                }
                // Allow natural BGP route flaps to slowly adjust the baseline
                tcb.expected_ttl = ip_ttl_val;
            }

            // RFC 3168 Explicit Congestion Notification (ECN) Reaction
            if (tcb.ecn_permitted) {
                uint8_t ip_tos_val;
                if constexpr (std::is_same_v<FrameType, raw_tcp_ipv4_frame>) ip_tos_val = hdr.ip_tos;
                else ip_tos_val = static_cast<uint8_t>((core::endian::network_to_host32(hdr.ipv6_flow) >> 20) & 0xFF);

                // CE (Congestion Encountered) in IP TOS
                if ((ip_tos_val & 0x03) == 0x03) {
                    // CRITICAL FIX: Axiom 21 - Monoidal Collapse via ECN Curvature Spoofing
                    // Restrict ECN topological response loop to strictly monotonic sequences
                    if (SL_EXPECT_TRUE(original_payload_len > 0 && host_seq == tcb.rcv_nxt)) {
                        tcb.temporal_flags |= TEMP_FLAG_ECN_ECHO;
                        tcb.flags_pending |= FLAG_ACK; // Force immediate ACK to report congestion
                    }
                }
                // Sender received ECE from Receiver -> Cut CWND
                if (hdr.tcp_flags & FLAG_ECE) {
                    if (tcb.cwnd > tcb.ssthresh) { // Halve CWND exactly once per RTT
                        // CRITICAL FIX: Axiom 28 - Lexicographical Underflow in Congestion Metric Scaling
                        uint32_t safe_cwnd = (tcb.cwnd >= 1460u) ? tcb.cwnd : 1460u;
                        tcb.ssthresh = (safe_cwnd >> 1) > 2920u ? (safe_cwnd >> 1) : 2920u;
                        tcb.cwnd = tcb.ssthresh;
                        tcb.temporal_flags |= TEMP_FLAG_CWR;
                    }
                }
                // Receiver received CWR from Sender -> Stop sending ECE
                if (hdr.tcp_flags & FLAG_CWR) {
                    tcb.temporal_flags &= ~TEMP_FLAG_ECN_ECHO;
                }
            }

            // CRITICAL FIX: Axiom 4 - Non-Commutative Evaluation Order in Protection Manifolds
            // Blind SYN must be checked before PAWS to prevent temporal sequence validation drops.
            if (SL_EXPECT_FALSE((hdr.tcp_flags & FLAG_SYN) && (tcb.phase_mask & (PHASE_ESTABLISHED | PHASE_FIN_WAIT1 | PHASE_FIN_WAIT2 | PHASE_CLOSE_WAIT | PHASE_CLOSING | PHASE_TIME_WAIT | PHASE_LAST_ACK)))) {
                bool allow_recycle = false;
                
                // TIME_WAIT Assassination (TCP_TW_REUSE)
                if (tcb.phase_mask == PHASE_TIME_WAIT) {
                    int32_t seq_diff = static_cast<int32_t>(host_seq - tcb.rcv_nxt);
                    if (seq_diff > 0) {
                        allow_recycle = true; // New connection on the same 4-tuple!
                        // Prevent Blind Session Hijacking via predictable ISN (0) on recycle
                        tcb.snd_una = tcp_syn_cookie::compute_mac(tcb.local_ipv4, tcb.local_port, tcb.remote_ipv4, tcb.remote_port, 0, tcp_syn_cookie::get_time_counter());
                        tcb.snd_nxt = tcb.snd_una;
                        tcb.dup_acks = 0;
                        tcb.temporal_flags = 0;
                        tcb.flags_pending = 0;
                        tcb.rto_retries = 0;
                        tcb.time_wait_deadline_tsc = 0;
                        tcb.rto_deadline_tsc = 0;
                    }
                }
                
                if (!allow_recycle) {
                    // RFC 5961 Blind SYN DoS Protection: Challenge ACK
                    trigger_challenge_ack(tcb);
                    payload_len = 0;
                    FLOW_DROP_TRUE("Blind SYN injected into active phase -> Challenge ACK");
                }
            }

            // Parse TCP Options (WScale, MSS)
            uint8_t data_offset = hdr.tcp_data_offset >> 4;
            if (SL_EXPECT_FALSE(data_offset > 5)) {
                const uint8_t* opt_ptr;
                if constexpr (std::is_same_v<FrameType, raw_tcp_ipv4_frame>) opt_ptr = reinterpret_cast<const uint8_t*>(&hdr) + 54;
                else opt_ptr = reinterpret_cast<const uint8_t*>(&hdr) + 74;
                
                int bytes_left = (data_offset - 5) * 4;
                while (bytes_left > 0) {
                    uint8_t kind = opt_ptr[0];
                    if (kind == 0) break; // EOL
                    if (kind == 1) { opt_ptr++; bytes_left--; continue; } // NOP
                    
                    // VULNERABILITY FIX: Prevent OOB read if only 1 byte is left!
                    if (SL_EXPECT_FALSE(bytes_left < 2)) break;

                    uint8_t len = opt_ptr[1];
                    if (len < 2 || len > bytes_left) break; // Malformed
                    
                    if (hdr.tcp_flags & FLAG_SYN) {
                        if (kind == 2 && len == 4) { // MSS
                            tcb.remote_mss = (opt_ptr[2] << 8) | opt_ptr[3];
                            // CRITICAL FIX: CVE-2019-11479 (TCP MSS Resource Exhaustion)
                            if (SL_EXPECT_FALSE(tcb.remote_mss < 536)) tcb.remote_mss = 536;
                        } else if (kind == 3 && len == 3) { // Window Scale
                            tcb.snd_wscale = opt_ptr[2] > 14 ? 14 : opt_ptr[2];
                            tcb.rcv_wscale = 7; // We advertise 128x scaling
                        } else if (kind == 4 && len == 2) { // SACK Permitted
                            tcb.sack_permitted = 1;
                        }
                    } else if (kind == 5 && tcb.sack_permitted) { // SACK Block (RFC 2018)
                        // CRITICAL FIX: Axiom 23 - Lexicographical Disjunction in SACK Block Extraction
                        if (SL_EXPECT_FALSE((len - 2) % 8 != 0)) break;
                        int num_blocks = (len - 2) / 8;
                        if (tcb.tx_unacked_ring) {
                            auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_unacked_ring);
                            for (int i = 0; i < num_blocks; ++i) {
                                uint32_t left_edge = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(opt_ptr + 2 + i * 8));
                                uint32_t right_edge = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(opt_ptr + 6 + i * 8));
                                
                                // CRITICAL FIX: CVE-2019-11478 (SACK Sloth CPU Exhaustion) & Blind Injection
                                // Drop malicious SACK blocks that acknowledge unsent data or data already ACKed natively via modulo.
                                uint32_t inflight = tcb.snd_nxt - tcb.snd_una;
                                // CRITICAL FIX: Axiom 8 - Lexicographical Disjunction in Historical D-SACK Underflow
                                if (SL_EXPECT_FALSE(static_cast<int32_t>(left_edge - tcb.snd_una) > static_cast<int32_t>(inflight) || 
                                                    static_cast<int32_t>(right_edge - tcb.snd_una) > static_cast<int32_t>(inflight) ||
                                                    static_cast<int32_t>(right_edge - left_edge) <= 0)) continue;

                                for (size_t k = 0; k < unacked->available_to_peek(); ++k) {
                                    auto* slot = unacked->get_peek_slot(k);
                                    uint32_t seq = core::endian::network_to_host32(slot->get_header()->tcp_seq);
                                    uint32_t end_seq = seq + slot->get_payload_length() + ((slot->get_header()->tcp_flags & (FLAG_SYN | FLAG_FIN)) ? 1 : 0);
                                    
                                    // If segment is fully encompassed by the SACK block
                                    // CRITICAL FIX: Axiom 33 - Lexicographical Wrap-Around in SACK Geometric Bounding
                                    if (static_cast<int32_t>(seq - left_edge) >= 0 && static_cast<int32_t>(right_edge - end_seq) >= 0) {
                                        const_cast<outbound_tcp_segment<1460>*>(slot)->is_sacked = true;
                                    }
                                }
                            }
                        }
                    } else if (kind == 8 && len == 10) { // Timestamp Option (RFC 7323)
                        uint32_t tsval = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(opt_ptr + 2));
                        if (hdr.tcp_flags & FLAG_SYN) tcb.ts_permitted = 1;
                        if (tcb.ts_permitted) {
                            // CRITICAL FIX: PAWS modulo arithmetic prevents permanent deadlock upon 32-bit TS wrap-around
                            int32_t ts_diff = static_cast<int32_t>(tcb.ts_recent - tsval);
                            // CRITICAL FIX: Axiom 7 - Orthogonal Temporal Bypass in Half-Space PAWS Boundaries
                            if (SL_EXPECT_FALSE((ts_diff > 0 || (tcb.ts_recent - tsval) == 0x80000000u) && host_seq <= tcb.rcv_nxt)) {
                                // CRITICAL FIX: Axiom 25 - Temporal Substrate Fracture in PAWS Lexicography
                                // Evaluate intersection with unacknowledged spatial window to exempt valid ACKs
                                bool valid_ack = (host_ack - tcb.snd_una <= tcb.snd_nxt - tcb.snd_una) && (host_ack != tcb.snd_una);
                                if (SL_EXPECT_FALSE(end_seq <= tcb.rcv_nxt && valid_ack)) {
                                    // Exclusion exemption: PAWS bypass to process valid unacknowledged intersections
                                } else {
                                    trigger_challenge_ack(tcb);
                                    payload_len = 0;
                                    FLOW_DROP_TRUE("PAWS Sequence Rejected");
                                }
                            }
                            if (host_seq <= tcb.rcv_nxt) {
                                tcb.ts_recent = tsval;
                            }
                        }
                    }
                    opt_ptr += len;
                    bytes_left -= len;
                }
            }

            // ACK processing
            if (SL_EXPECT_TRUE(hdr.tcp_flags & FLAG_ACK)) {
                if (SL_EXPECT_FALSE(tcb.phase_mask == PHASE_SYN_SENT)) {
                    // Client Mode: Transition to ESTABLISHED upon valid SYN-ACK reception
                    if (hdr.tcp_flags & FLAG_SYN) {
                        if constexpr (std::is_same_v<FrameType, raw_tcp_ipv4_frame>) tcb.expected_ttl = hdr.ip_ttl; // Baseline from SYN-ACK
                        else tcb.expected_ttl = hdr.ipv6_hlim;

                        if (hdr.tcp_flags & FLAG_ECE) tcb.ecn_permitted = 1; // ECN Negotiated
                        tcb.phase_mask = PHASE_ESTABLISHED;
                        tcb.rcv_nxt = host_seq + 1;
                        tcb.snd_una = host_ack;
                        tcb.cwnd = 1460u * 10;
                        tcb.ssthresh = 65535u;
                        tcb.snd_wnd = static_cast<uint32_t>(host_wnd); // RFC 7323: SYN window is NEVER scaled
                        
                        tcb.flags_pending |= FLAG_ACK;
                        payload_len = 0;
                        FLOW_DROP_TRUE("SYN-ACK Client Mode Rehydrated");
                    }
                } else if (tcb.phase_mask == PHASE_SYN_RCVD) {
                // CRITICAL FIX: Axiom 7 - Asymmetric State Mutability via Unverified Acknowledgment Projections
                int32_t acked_bytes_signed = static_cast<int32_t>(host_ack - tcb.snd_una);
                uint32_t inflight = tcb.snd_nxt - tcb.snd_una;
                if (SL_EXPECT_FALSE(acked_bytes_signed <= 0 || static_cast<uint32_t>(acked_bytes_signed) > inflight)) {
                    trigger_challenge_ack(tcb);
                        payload_len = 0;
                        FLOW_DROP_TRUE("Invalid ACK in SYN-RCVD -> Challenge ACK");
                    }
                    
                    tcb.phase_mask = PHASE_ESTABLISHED;
                    tcb.cwnd = 1460u * 10;   // Initial Window (IW10)
                    tcb.ssthresh = 65535u;   // Infinite initial threshold
                    
                    // CRITICAL FIX: Axiom 1 - Topological Desynchronization in Simultaneous Open Morphisms
                    tcb.snd_una = host_ack;
                    tcb.snd_nxt = host_ack;
                    if constexpr (std::is_same_v<FrameType, raw_tcp_ipv4_frame>) tcb.expected_ttl = hdr.ip_ttl;
                    else tcb.expected_ttl = hdr.ipv6_hlim;

                    if (SL_EXPECT_TRUE(tcb.ooo_matrix)) {
                        static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix)->reset(tcb.rcv_nxt);
                    }
                }
                
                uint32_t current_wnd = static_cast<uint32_t>(host_wnd) << ((hdr.tcp_flags & FLAG_SYN) ? 0 : tcb.snd_wscale);
                uint32_t was_fast_recovery = (tcb.dup_acks >= 3) ? 1 : 0;
                uint32_t is_dup = (host_ack == tcb.snd_una && original_payload_len == 0 && current_wnd == tcb.snd_wnd && !(hdr.tcp_flags & (FLAG_SYN | FLAG_FIN))) ? 1 : 0;
                tcb.dup_acks = (tcb.dup_acks + is_dup) * is_dup;
                
                if (SL_EXPECT_FALSE(tcb.dup_acks == 3)) {
                    // Fast Retransmit / Fast Recovery Entry
                    // CRITICAL FIX: Axiom 28 - Lexicographical Underflow in Congestion Metric Scaling
                    uint32_t safe_cwnd = (tcb.cwnd >= 1460u) ? tcb.cwnd : 1460u;
                    tcb.ssthresh = (safe_cwnd >> 1) > 2920u ? (safe_cwnd >> 1) : 2920u;
                    tcb.cwnd = tcb.ssthresh + 4380u; // Inflate by 3 MSS
                    tcb.recover = tcb.snd_nxt; // RFC 6582 NewReno Horizon
                    tcb.highest_rtx_seq = 0;
                    
                    // Standard TCP Fast Retransmit: Pop oldest from sliding window into the wire
                    if (tcb.tx_unacked_ring && tcb.tx_egress_conduit) {
                        auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_unacked_ring);

                        auto* egress = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_egress_conduit);
                        
                        if (unacked->available_to_peek() > 0) {
                            const outbound_tcp_segment<1460>* lost_frame = unacked->get_peek_slot(0);
                            outbound_tcp_segment<1460>* slot = egress->get_reserved_slot(0);
                            if (slot) {
                                uint32_t seg_seq = core::endian::network_to_host32(lost_frame->get_header()->tcp_seq);
                                uint32_t seg_len = lost_frame->get_payload_length() + ((lost_frame->get_header()->tcp_flags & (FLAG_SYN | FLAG_FIN)) ? 1 : 0);
                                tcb.highest_rtx_seq = seg_seq + seg_len;

                                slot->mbuf = lost_frame->mbuf;
                                if (SL_EXPECT_FALSE(!slot->retain())) {
                                    tcb.phase_mask = PHASE_CLOSED;
                                    tcb.flags_pending |= FLAG_RST;
                                    return true;
                                }
                                
                                slot->get_header()->tcp_ack = core::endian::host_to_network32(tcb.rcv_nxt);
                                uint32_t wnd = tcb.rcv_wnd >> tcb.rcv_wscale;
                                slot->get_header()->tcp_window = core::endian::host_to_network16(wnd > 65535 ? 65535 : wnd);
                                
                                auto* rtx_hdr = slot->get_header();
                                rtx_hdr->ip_checksum = 0;
                                rtx_hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(rtx_hdr) + 14, 20, 0); // Native Software Fallback
                                uint16_t tcp_len = core::endian::network_to_host16(rtx_hdr->ip_len) - ((rtx_hdr->ip_ihl_ver & 0x0F) * 4);
                                uint32_t pseudo_sum = 0;
                                uint32_t src = rtx_hdr->ip_src;
                                uint32_t dst = rtx_hdr->ip_dst;
                                pseudo_sum += (src & 0xFFFF) + (src >> 16);
                                pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
                                pseudo_sum += core::endian::host_to_network16(6);
                                pseudo_sum += core::endian::host_to_network16(tcp_len);
                                rtx_hdr->tcp_checksum = 0;
                                rtx_hdr->tcp_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(rtx_hdr) + 34, tcp_len, pseudo_sum);

                                egress->commit_n(1);
                            }
                        }
                    }
                } else if (tcb.dup_acks > 3) {
                    // CRITICAL FIX: Axiom 6 - Symplectic Rupture via Infinite CWND Lexicographical Inflation
                    if (SL_EXPECT_TRUE(tcb.cwnd <= 0xFFFFFFFFu - 1460u)) {
                        tcb.cwnd += 1460u; // Fast Recovery artificial inflation
                    }
                } else {
                    uint32_t inflight = tcb.snd_nxt - tcb.snd_una;
                    // CRITICAL FIX: Axiom 5 - Unsigned Cohomology Projection in Old Acknowledgment Vectors
                    int32_t acked_bytes_signed = static_cast<int32_t>(host_ack - tcb.snd_una);
                    
                    if (acked_bytes_signed > 0 && static_cast<uint32_t>(acked_bytes_signed) <= inflight) {
                        bool is_partial_ack = false;
                        if (SL_EXPECT_FALSE(was_fast_recovery)) {
                            // CRITICAL FIX: Axiom 15 - Cyclic Lexicographical Underflow in Unacknowledged Sequences
                            if (static_cast<int32_t>(tcb.recover - host_ack) > 0) {
                                is_partial_ack = true;
                            } else {
                                tcb.cwnd = tcb.ssthresh; // Deflate window: Exit Fast Recovery!
                                tcb.dup_acks = 0;
                            }
                        }
                    
                    tcb.snd_una = host_ack;
                    
                    // CRITICAL FIX: Axiom 9 - Persistent Topological Rupture in Retransmission Posets
                    // Karn's Algorithm prevents RTT estimation on retransmitted segments, but
                    // any valid spatial progression MUST collapse the RTO exhaustion gradient.
                    tcb.rto_retries = 0;

                    if (host_ack == tcb.snd_nxt) {
                        tcb.temporal_flags &= ~TEMP_FLAG_RETRANSMIT; // All flight cleared
                    }
                    
                    tcb.rto_deadline_tsc = 0; // Trigger timer auto-arm on next tick
                    
                    // Standard TCP Sliding Window Consumption
                    if (tcb.tx_unacked_ring) {
                        auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_unacked_ring);
                        while (unacked->available_to_peek() > 0) {
                            uint32_t seg_seq = core::endian::network_to_host32(unacked->get_peek_slot(0)->get_header()->tcp_seq);
                            uint32_t seg_len = unacked->get_peek_slot(0)->get_payload_length();
                            uint32_t seg_end = seg_seq + seg_len + ((unacked->get_peek_slot(0)->get_header()->tcp_flags & (FLAG_SYN | FLAG_FIN)) ? 1 : 0);
                            
                            // Consume ONLY if the segment has been FULLY encompassed by the ACK horizon
                            if (tcb.snd_una - seg_end <= 0x7FFFFFFF) {
                                const_cast<outbound_tcp_segment<1460>*>(unacked->get_peek_slot(0))->release(); // Hardware buffer returned to mempool natively
                                unacked->consume_n(1);
                            } else {
                                break;
                            }
                        }
                    }
                    
                    // State Transitions for active connection teardown
                    if (tcb.phase_mask == PHASE_FIN_WAIT1 && tcb.snd_una == tcb.snd_nxt) {
                        tcb.phase_mask = PHASE_FIN_WAIT2;
                        tcb.time_wait_deadline_tsc = 0;
                    } else if (tcb.phase_mask == PHASE_CLOSING && tcb.snd_una == tcb.snd_nxt) {
                        tcb.phase_mask = PHASE_TIME_WAIT;
                        tcb.time_wait_deadline_tsc = 0;
                    } else if (tcb.phase_mask == PHASE_LAST_ACK && tcb.snd_una == tcb.snd_nxt) {
                        tcb.phase_mask = PHASE_CLOSED;
                    }

                    if (SL_EXPECT_FALSE(is_partial_ack)) {
                        // RFC 6582 NewReno: Retransmit the NEXT unacknowledged segment immediately
                        // CRITICAL FIX: Axiom 3 - Lexicographical Desynchronization in SACK Geometric Extraction
                        if (tcb.highest_rtx_seq != 0 && static_cast<int32_t>(host_ack - tcb.highest_rtx_seq) < 0) {
                            // Yield to prevent infinite geometric extraction of the same MBUF.
                        } else {
                            if (tcb.tx_unacked_ring && tcb.tx_egress_conduit) {
                                auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_unacked_ring);
                                auto* egress = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_egress_conduit);
                                
                                const outbound_tcp_segment<1460>* lost_frame = nullptr;
                                for (size_t k = 0; k < unacked->available_to_peek(); ++k) {
                                    if (!unacked->get_peek_slot(k)->is_sacked) {
                                        lost_frame = unacked->get_peek_slot(k);
                                        break;
                                    }
                                }

                                if (lost_frame) {
                                    uint32_t seg_seq = core::endian::network_to_host32(lost_frame->get_header()->tcp_seq);
                                    uint32_t seg_len = lost_frame->get_payload_length() + ((lost_frame->get_header()->tcp_flags & (FLAG_SYN | FLAG_FIN)) ? 1 : 0);
                                    tcb.highest_rtx_seq = seg_seq + seg_len;

                                    outbound_tcp_segment<1460>* slot = egress->get_reserved_slot(0);
                                    if (slot) {
                                        slot->mbuf = lost_frame->mbuf;
                                        if (SL_EXPECT_FALSE(!slot->retain())) {
                                            tcb.phase_mask = PHASE_CLOSED;
                                            tcb.flags_pending |= FLAG_RST;
                                            FLOW_DROP_TRUE("MBUF pool exhaustion during Fast Retransmit");
                                        }
                                        
                                        slot->get_header()->tcp_ack = core::endian::host_to_network32(tcb.rcv_nxt);
                                        uint32_t wnd = tcb.rcv_wnd >> tcb.rcv_wscale;
                                        slot->get_header()->tcp_window = core::endian::host_to_network16(wnd > 65535 ? 65535 : wnd);
                                        
                                        auto* rtx_hdr = slot->get_header();
                                        rtx_hdr->ip_checksum = 0;
                                        rtx_hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(rtx_hdr) + 14, 20, 0); // Native Software Fallback
                                        uint16_t tcp_len = core::endian::network_to_host16(rtx_hdr->ip_len) - ((rtx_hdr->ip_ihl_ver & 0x0F) * 4);
                                        uint32_t pseudo_sum = 0;
                                        uint32_t src = rtx_hdr->ip_src;
                                        uint32_t dst = rtx_hdr->ip_dst;
                                        pseudo_sum += (src & 0xFFFF) + (src >> 16);
                                        pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
                                        pseudo_sum += core::endian::host_to_network16(6);
                                        pseudo_sum += core::endian::host_to_network16(tcp_len);
                                        rtx_hdr->tcp_checksum = 0;
                                        rtx_hdr->tcp_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(rtx_hdr) + 34, tcp_len, pseudo_sum);
                                        egress->commit_n(1);
                                    }
                                }
                            }
                        }
                    } else {
                        // Branchless Congestion Window Advancement (AIMD)
                        // Axiom 25: Geometric Null-Space Collapse in Congestion Divisions
                        uint32_t safe_cwnd = (tcb.cwnd >= 1460u) ? tcb.cwnd : 1460u;
                        uint32_t is_ss = (safe_cwnd < tcb.ssthresh) ? 1 : 0;
                        uint32_t ca_inc = (1460u * 1460u) / safe_cwnd;
                        ca_inc = ca_inc | (ca_inc == 0); // Branchless max(1, ca_inc)
                        
                        tcb.cwnd = safe_cwnd + (is_ss * 1460u) + ((1 - is_ss) * ca_inc);
                    }
                    
                    tcb.snd_wnd = current_wnd; // Valid progressing ACK Window Update
                    } else if (acked_bytes_signed > 0 && static_cast<uint32_t>(acked_bytes_signed) > inflight) {
                        // CRITICAL FIX: Unacknowledged Data Spoofing (CVE-1999-0162)
                        // RFC 793: If the ACK acknowledges data we haven't sent, we MUST send a Challenge ACK.
                        trigger_challenge_ack(tcb);
                        payload_len = 0;
                        FLOW_DROP_TRUE("ACK acknowledges unsent data -> Challenge ACK");
                    } else if (is_dup == 0 && host_seq == tcb.rcv_nxt) {
                        tcb.snd_wnd = current_wnd; // Pure Window Update
                    }
                }
            }

            // RST Processing (Immediate Teardown & Zombie Prevention)
            if (SL_EXPECT_FALSE(hdr.tcp_flags & FLAG_RST)) {
                // RFC 1337: TIME-WAIT Assassination Hazard Prevention
                // We MUST entirely ignore RSTs while in TIME_WAIT to ensure the 2MSL timer completes.
                if (SL_EXPECT_FALSE(tcb.phase_mask == PHASE_TIME_WAIT)) {
                    payload_len = 0;
                    FLOW_DROP_TRUE("RST ignored in TIME_WAIT");
                }

                // RFC 5961 Blind Reset Protection & Unsynchronized State Loop Avoidance
                if (host_seq == tcb.rcv_nxt || tcb.phase_mask == PHASE_SYN_SENT) {
                    tcb.phase_mask = PHASE_CLOSED;
                } else {
                    // In-window but not exact sequence: Challenge ACK
                    trigger_challenge_ack(tcb);
                }
                payload_len = 0;
                FLOW_DROP_TRUE("RST processed");
            }

            // SYN processing (Branchless state injection)
            if (SL_EXPECT_FALSE(hdr.tcp_flags & FLAG_SYN)) {
                tcb.rcv_nxt = host_seq + 1;
                
                if (tcb.phase_mask == PHASE_SYN_SENT) {
                    tcb.phase_mask = PHASE_SYN_RCVD;
                    tcb.flags_pending |= FLAG_SYN | FLAG_ACK; // Schedule SYN-ACK natively
                    payload_len = 0;
                    FLOW_DROP_TRUE("Simultaneous Open SYN-ACK Scheduled");
                }

                tcb.phase_mask = PHASE_SYN_RCVD;
                
                // CRITICAL FIX: "TCP Resurrection" Session Hijack (SYN-Based)
                // When recycling a TIME_WAIT connection, we MUST generate a SYN-ACK 
                // and discard any smuggled payload to prevent session poisoning.
                tcb.flags_pending |= FLAG_SYN | FLAG_ACK;
                payload_len = 0;
                
                if (SL_EXPECT_TRUE(tcb.ooo_matrix)) {
                    static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix)->reset(tcb.rcv_nxt);
                }
                FLOW_DROP_TRUE("Zombie SYN injection scheduled");
            }

            // Adjust host_seq for payload processing if SYN was consumed
            uint32_t payload_seq = host_seq + ((hdr.tcp_flags & FLAG_SYN) ? 1 : 0);

            // CRITICAL FIX: Axiom 6 - Topological Fracture in Unacknowledged State Transitions
            if (SL_EXPECT_FALSE(!(hdr.tcp_flags & FLAG_ACK) && 
                (tcb.phase_mask & (PHASE_SYN_RCVD | PHASE_ESTABLISHED | PHASE_FIN_WAIT1 | PHASE_FIN_WAIT2 | PHASE_CLOSE_WAIT | PHASE_CLOSING | PHASE_LAST_ACK | PHASE_TIME_WAIT)))) {
                payload_len = 0;
                FLOW_DROP_FALSE("Missing ACK flag on active connection segment");
            }

            // Fast Path Payload Advancer & OOO Handling
            // CRITICAL FIX: Axiom 8 - Topological Exclusion of Orthogonal Finite Geometries (Pure OOO FIN)
            if (SL_EXPECT_TRUE((tcb.phase_mask & (PHASE_ESTABLISHED | PHASE_FIN_WAIT1 | PHASE_FIN_WAIT2 | PHASE_SYN_RCVD)) && (payload_len > 0 || (hdr.tcp_flags & FLAG_FIN)))) {
                if (payload_seq == tcb.rcv_nxt) {
                    tcb.rcv_nxt += payload_len; // In-order reception
                    for (int i = 0; i < 3; ++i) {
                        if (tcb.sack_valid[i]) {
                            // CRITICAL FIX: Axiom 11 - Asymmetric Cohomology in SACK Inversion Posets
                            if (static_cast<int32_t>(tcb.rcv_nxt - tcb.sack_right_edges[i]) >= 0) {
                                // Block is fully encompassed / left behind
                                tcb.sack_valid[i] = false;
                            } else if (static_cast<int32_t>(tcb.rcv_nxt - tcb.sack_left_edges[i]) >= 0) {
                                // Block partially consumed, slide the left edge up
                                tcb.sack_left_edges[i] = tcb.rcv_nxt;
                            }
                        }
                    }
                    
                    // Axiom 17 & 22: Null-Space Projection in Contiguous Defragmentation Boundaries & Lexicographical Monoidal Expansion
                    if (hdr.tcp_flags & (FLAG_PSH | FLAG_FIN)) {
                        tcb.flags_pending |= FLAG_ACK;
                        tcb.quick_ack_cnt = 0;
                    } else {
                        tcb.quick_ack_cnt++;
                        if (tcb.quick_ack_cnt >= 2) {
                            tcb.flags_pending |= FLAG_ACK;
                            tcb.quick_ack_cnt = 0;
                        } else {
                            tcb.temporal_flags |= TEMP_FLAG_ACK_PENDING; // Arm delayed ACK
                        }
                    }
                } else if (payload_seq - tcb.rcv_nxt <= 0x7FFFFFFF) {
                    // Out-Of-Order Segment
                    if (SL_EXPECT_TRUE(tcb.ooo_matrix && mbuf && mbuf_absorbed)) {
                        auto* ooo = static_cast<tcp_spatial_ooo_matrix*>(tcb.ooo_matrix);
                        *mbuf_absorbed = ooo->insert_mbuf(tcb.rcv_nxt, payload_seq, mbuf, payload_offset, payload_len, hdr.tcp_flags);
                        
                        uint32_t left = payload_seq;
                        uint32_t right = payload_seq + payload_len;
                        
                        bool merged = false;
                        for (int i = 0; i < 3; ++i) {
                            if (tcb.sack_valid[i]) {
                                // Safe 32-bit wrap-around intersection check
                                if (static_cast<int32_t>(left - tcb.sack_right_edges[i]) <= 0 && 
                                    static_cast<int32_t>(right - tcb.sack_left_edges[i]) >= 0) {
                                    
                                    if (static_cast<int32_t>(left - tcb.sack_left_edges[i]) < 0) tcb.sack_left_edges[i] = left;
                                    if (static_cast<int32_t>(right - tcb.sack_right_edges[i]) > 0) tcb.sack_right_edges[i] = right;
                                    merged = true;
                                    break;
                                }
                            }
                        }
                        if (!merged) {
                            tcb.sack_left_edges[2] = tcb.sack_left_edges[1];
                            tcb.sack_right_edges[2] = tcb.sack_right_edges[1];
                            tcb.sack_valid[2] = tcb.sack_valid[1];
                            tcb.sack_left_edges[1] = tcb.sack_left_edges[0];
                            tcb.sack_right_edges[1] = tcb.sack_right_edges[0];
                            tcb.sack_valid[1] = tcb.sack_valid[0];
                            tcb.sack_left_edges[0] = left;
                            tcb.sack_right_edges[0] = right;
                            tcb.sack_valid[0] = true;
                        }
                    }
                    // RFC 5681: Out-of-order must trigger immediate duplicate ACK
                    tcb.flags_pending |= FLAG_ACK;
                    payload_len = 0; // Hide from immediate defragmenter pass
                } else {
                    // CRITICAL FIX: Axiom 13 - Non-Commutative Evaluation in OOO Boundary Folds
                    int32_t overlap = static_cast<int32_t>(tcb.rcv_nxt - payload_seq);
                    if (SL_EXPECT_FALSE(overlap < 0)) {
                        FLOW_DROP_FALSE("Absolute geometric sequence leap rejected");
                    }
                    if (static_cast<uint32_t>(overlap) < payload_len) {
                        trim_offset = static_cast<uint32_t>(overlap);
                        payload_len -= trim_offset;
                        tcb.rcv_nxt += payload_len; // In-order reception of the tail
                        
                        if (hdr.tcp_flags & (FLAG_PSH | FLAG_FIN)) {
                            tcb.flags_pending |= FLAG_ACK;
                            tcb.quick_ack_cnt = 0;
                        } else {
                            tcb.quick_ack_cnt++;
                            if (tcb.quick_ack_cnt >= 2) {
                                tcb.flags_pending |= FLAG_ACK;
                                tcb.quick_ack_cnt = 0;
                            } else {
                                tcb.temporal_flags |= TEMP_FLAG_ACK_PENDING; // Arm delayed ACK
                            }
                        }
                    } else {
                        // Full Duplicate or old segment
                        tcb.flags_pending |= FLAG_ACK;
                        payload_len = 0;
                    }
                }
            }

            // FIN processing (Strictly in-order)
            if (SL_EXPECT_FALSE(hdr.tcp_flags & FLAG_FIN)) {
                if (payload_seq + original_payload_len == tcb.rcv_nxt) {
                    tcb.rcv_nxt++; // FIN consumes a sequence number
                    tcb.flags_pending |= FLAG_ACK;
                    if (tcb.phase_mask == PHASE_ESTABLISHED || tcb.phase_mask == PHASE_SYN_RCVD) tcb.phase_mask = PHASE_CLOSE_WAIT;
                    else if (tcb.phase_mask == PHASE_FIN_WAIT1) tcb.phase_mask = PHASE_CLOSING; // Simultaneous Close
                    else if (tcb.phase_mask == PHASE_FIN_WAIT2) {
                        tcb.phase_mask = PHASE_TIME_WAIT;
                        tcb.time_wait_deadline_tsc = 0; // Reset timer for new state
                    }
                } else if (tcb.phase_mask == PHASE_TIME_WAIT) {
                    // RFC 793: TIME_WAIT Timer Assassination Prevention
                    // Duplicate FIN received (our ACK was lost). Restart 2MSL timer.
                    tcb.time_wait_deadline_tsc = 0;
                    tcb.flags_pending |= FLAG_ACK;
                }
            }

            // CRITICAL FIX: Axiom 3 - Temporal Null-Space in TIME_WAIT Geometries
            if (SL_EXPECT_FALSE(tcb.phase_mask == PHASE_TIME_WAIT && original_payload_len > 0)) {
                trigger_challenge_ack(tcb);
                payload_len = 0;
            }

            return true;
        }

        /**
         * @brief Hydrates the TCP/IP header fields for an outbound segment.
         * @details Decoupled from physical buffer management to support stateless 
         * generation and logic-only unit testing.
         */
        template <typename FrameType>
        static SLAB_HOT uint32_t prepare_outbound_header(tcp_transmission_control_block& tcb, FrameType& hdr, uint32_t payload_len, uint8_t flags) noexcept {
            uint8_t opt_len = 0;
            if (flags & FLAG_SYN) opt_len += 12;
            if (tcb.ts_permitted) opt_len += 12;
            
            uint8_t num_sack_blocks = 0;
            if (tcb.sack_permitted && !(flags & FLAG_SYN)) {
                for (int i = 0; i < 3; ++i) {
                    if (tcb.sack_valid[i]) num_sack_blocks++;
                }
            }
            // CRITICAL FIX: SACK Options consume 4 bytes of header (NOP, NOP, Kind, Len) + Blocks
            if (num_sack_blocks > 0) opt_len += 4 + (num_sack_blocks * 8);

            uint8_t rem = opt_len % 4;
            if (rem != 0) opt_len += (4 - rem); // Ensure 32-bit alignment

            // CRITICAL FIX: Axiom 5 - Asymmetric Gauge Field Collapse in L4 Header Formulation
            if (SL_EXPECT_FALSE(payload_len > 65535u - 40u - opt_len)) {
                payload_len = 65535u - 40u - opt_len;
            }

            // L2 Ethernet Encapsulation
            __builtin_memcpy(hdr.dest_mac, tcb.target_mac, 6);

            if constexpr (std::is_same_v<FrameType, raw_tcp_ipv4_frame>) {
                hdr.eth_type = core::endian::network_to_host16(0x0800);
                hdr.ip_ihl_ver = 0x45;
                hdr.ip_tos = tcb.ecn_permitted ? 0x02 : 0; // ECT(0)
                hdr.ip_len = core::endian::network_to_host16(40 + opt_len + payload_len);
                hdr.ip_id = 0;
                hdr.ip_frag_offset = core::endian::network_to_host16(0x4000);
                hdr.ip_ttl = 64;
                hdr.ip_protocol = 6;
                hdr.ip_src = tcb.local_ipv4;
                hdr.ip_dst = tcb.remote_ipv4;
                hdr.ip_checksum = 0;
                hdr.ip_checksum = tcp_wire_engine::compute_checksum(&hdr.ip_ihl_ver, 20, 0); // Native Software Fallback
            } else {
                hdr.eth_type = core::endian::network_to_host16(0x86DD);
                uint32_t flow = 0x60000000;
                if (tcb.ecn_permitted) flow |= (0x02 << 20);
                hdr.ipv6_flow = core::endian::host_to_network32(flow);
                hdr.ipv6_plen = core::endian::network_to_host16(20 + opt_len + payload_len);
                hdr.ipv6_nxt = 6;
                hdr.ipv6_hlim = 64;
                hdr.ipv6_src[0] = tcb.local_ipv6[0];
                hdr.ipv6_src[1] = tcb.local_ipv6[1];
                hdr.ipv6_dst[0] = tcb.remote_ipv6[0];
                hdr.ipv6_dst[1] = tcb.remote_ipv6[1];
            }

            // L4 TCP Encapsulation & Checksum
            hdr.tcp_src_port = tcb.local_port;
            hdr.tcp_dst_port = tcb.remote_port;
            
            uint32_t seq = tcb.snd_nxt;
            bool is_probe = false;
            if (SL_EXPECT_FALSE(tcb.temporal_flags & TEMP_FLAG_KEEPALIVE_PROBE)) {
                // CRITICAL FIX: Axiom 20 - Mathematical Rupture in Non-Vanishing Temporal Probe Windows
                seq = (seq == 0) ? 0xFFFFFFFF : seq - 1; // Explicitly map 32-bit wrap-around
                tcb.temporal_flags &= ~TEMP_FLAG_KEEPALIVE_PROBE;
                is_probe = true;
            }
            hdr.tcp_seq = core::endian::network_to_host32(seq);
            hdr.tcp_ack = core::endian::network_to_host32(tcb.rcv_nxt);
            hdr.tcp_data_offset = ((20 + opt_len) / 4) << 4;
            
            uint8_t out_flags = flags;
            // The ACK flag is set on all packets except the initial SYN of an active open.
            if (!(tcb.phase_mask == PHASE_SYN_SENT && flags == FLAG_SYN)) {
                out_flags |= FLAG_ACK;
            }
            if (tcb.temporal_flags & TEMP_FLAG_ECN_ECHO) out_flags |= FLAG_ECE;
            if (tcb.temporal_flags & TEMP_FLAG_CWR) {
                out_flags |= FLAG_CWR;
                tcb.temporal_flags &= ~TEMP_FLAG_CWR;
            }
            if ((flags & FLAG_SYN) && tcb.ecn_permitted) {
                out_flags |= FLAG_ECE | FLAG_CWR; // Negotiate ECN
            }
            hdr.tcp_flags = out_flags;
            
            // Dynamic Flow Control Backpressure & Receiver-Side SWS Avoidance
            uint32_t wnd = tcb.rcv_wnd;
            if (tcb.rx_stream_ring) {
                auto* rx_ring = static_cast<core::spsc_ring_conduit<char, 4096>*>(tcb.rx_stream_ring);
                uint32_t used = rx_ring->available_to_peek();
                wnd = (4096 > used) ? (4096 - used) : 0;
                
                uint32_t mss = (tcb.remote_mss > 0) ? tcb.remote_mss : 1460;
                
                // CRITICAL FIX: Axiom 12 - Monoidal Boundary Truncation in SWS Hysteresis
                uint32_t scaled_wnd = wnd >> tcb.rcv_wscale;
                uint32_t scaled_mss = mss >> tcb.rcv_wscale;
                uint32_t scaled_threshold = (4096 / 2) >> tcb.rcv_wscale;
                
                if ((tcb.rcv_wnd >> tcb.rcv_wscale) == 0) {
                    if (scaled_wnd < scaled_threshold) wnd = 0; // Hysteresis lock
                } else if (scaled_wnd < scaled_mss && scaled_wnd < scaled_threshold) {
                    wnd = 0; // SWS Clamp
                }
            }
            tcb.rcv_wnd = wnd; // Update state
            wnd >>= tcb.rcv_wscale;
            hdr.tcp_window = core::endian::network_to_host16(wnd > 65535 ? 65535 : wnd);
            hdr.tcp_urgent_ptr = 0;
            
            if (SL_EXPECT_FALSE(opt_len > 0)) {
                uint8_t* out_opt;
                if constexpr (std::is_same_v<FrameType, raw_tcp_ipv4_frame>) out_opt = reinterpret_cast<uint8_t*>(&hdr) + 54;
                else out_opt = reinterpret_cast<uint8_t*>(&hdr) + 74;

                int cursor = 0;

                if (flags & FLAG_SYN) {
                    out_opt[cursor++] = 0x02; out_opt[cursor++] = 0x04;
                    out_opt[cursor++] = 0x05; out_opt[cursor++] = 0xB4; // MSS 1460
                    out_opt[cursor++] = 0x01; // NOP
                    out_opt[cursor++] = 0x03; out_opt[cursor++] = 0x03;
                    out_opt[cursor++] = 0x07; // WScale 7
                    out_opt[cursor++] = 0x04; out_opt[cursor++] = 0x02; // SACK Permitted
                    out_opt[cursor++] = 0x01; out_opt[cursor++] = 0x01; // NOP NOP
                }
                if (tcb.ts_permitted) {
                    tcb.ts_val++; // Increment monotonic TS clock
                    out_opt[cursor++] = 0x01; out_opt[cursor++] = 0x01; // NOP NOP
                    out_opt[cursor++] = 0x08; out_opt[cursor++] = 0x0A; // TS Kind, Len
                    uint32_t tsval_net = core::endian::host_to_network32(tcb.ts_val);
                    uint32_t tsrec_net = core::endian::host_to_network32(tcb.ts_recent);
                    __builtin_memcpy(&out_opt[cursor], &tsval_net, 4);
                    __builtin_memcpy(&out_opt[cursor+4], &tsrec_net, 4);
                    cursor += 8;
                }
                if (num_sack_blocks > 0) {
                    out_opt[cursor++] = 0x01; out_opt[cursor++] = 0x01; // NOP NOP
                    out_opt[cursor++] = 0x05; 
                    out_opt[cursor++] = 2 + (num_sack_blocks * 8); // SACK Kind, Len
                for (int i = 0; i < 3; ++i) {
                    if (tcb.sack_valid[i]) {
                        // CRITICAL FIX: Axiom 19 - Lexicographical Disjunction in Unaligned SACK Options
                        uint32_t left_net = core::endian::host_to_network32(tcb.sack_left_edges[i]);
                        uint32_t right_net = core::endian::host_to_network32(tcb.sack_right_edges[i]);
                        __builtin_memcpy(&out_opt[cursor], &left_net, 4);
                        __builtin_memcpy(&out_opt[cursor+4], &right_net, 4);
                        cursor += 8;
                    }
                    }
                }
                while (cursor < opt_len) {
                    out_opt[cursor++] = 0x01; // NOP padding
                }
            }

            uint16_t tcp_len = 20 + opt_len + payload_len;
            
            // Pre-zero the payload area BEFORE checksum computation to avoid invalidating the checksum
            if (SL_EXPECT_FALSE(is_probe && payload_len > 0)) {
                if constexpr (std::is_same_v<FrameType, raw_tcp_ipv4_frame>) std::memset(reinterpret_cast<char*>(&hdr) + 54 + opt_len, 0, payload_len);
                else std::memset(reinterpret_cast<char*>(&hdr) + 74 + opt_len, 0, payload_len);
            }

            uint32_t pseudo_sum = 0;
            if constexpr (std::is_same_v<FrameType, raw_tcp_ipv4_frame>) {
                uint32_t src = hdr.ip_src;
                uint32_t dst = hdr.ip_dst;
                pseudo_sum += (src & 0xFFFF) + (src >> 16);
                pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
            } else {
                const uint16_t* src16 = reinterpret_cast<const uint16_t*>(hdr.ipv6_src);
                const uint16_t* dst16 = reinterpret_cast<const uint16_t*>(hdr.ipv6_dst);
                for(int i=0; i<8; ++i) pseudo_sum += src16[i];
                for(int i=0; i<8; ++i) pseudo_sum += dst16[i];
            }
            pseudo_sum += core::endian::host_to_network16(6);
            pseudo_sum += core::endian::host_to_network16(tcp_len);
            
            // Performance Fix: Skip garbage checksumming if payload is injected downstream (Virtual Socket)
            hdr.tcp_checksum = 0;
            if (SL_EXPECT_TRUE(is_probe || payload_len == 0)) {
                hdr.tcp_checksum = tcp_wire_engine::compute_checksum(&hdr.tcp_src_port, tcp_len, pseudo_sum);
            }

            if (!is_probe) {
                tcb.snd_nxt += payload_len + ((flags & (FLAG_SYN | FLAG_FIN)) ? 1 : 0);
            }
            
            return payload_len;
        }

        static SLAB_HOT void prepare_outbound(tcp_transmission_control_block& tcb, outbound_tcp_segment<1460>& slot, uint32_t payload_len, uint8_t flags) noexcept {
            if (tcb.is_ipv6) {
                auto* hdr6 = reinterpret_cast<raw_tcp_ipv6_frame*>(slot.get_header());
                payload_len = prepare_outbound_header(tcb, *hdr6, payload_len, flags);
                slot.set_payload_length(payload_len, true);
            } else {
                payload_len = prepare_outbound_header(tcb, *slot.get_header(), payload_len, flags);
                slot.set_payload_length(payload_len, false);
            }
        }

        /**
         * @brief Natively flushes any pending standalone control frames (ACK, RST, ZWP).
         */
        static SLAB_HOT void dispatch_control_frames(tcp_transmission_control_block& tcb) noexcept {
            if (SL_EXPECT_FALSE(tcb.flags_pending != 0 && tcb.tx_egress_conduit && tcb.tx_mbuf_pool)) {
                
                // Axiom 16: Functorial Collision in Asymmetric State Generation
                if (SL_EXPECT_FALSE(tcb.flags_pending & FLAG_RST)) {
                    tcb.flags_pending = FLAG_RST; // Annihilate orthogonal topologies
                }
                
                // Deadlock Prevention: We must guarantee unacked tracking space BEFORE we mutate snd_nxt
                if (SL_EXPECT_FALSE(tcb.flags_pending & (FLAG_SYN | FLAG_FIN))) {
                    if (tcb.tx_unacked_ring) {
                        auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_unacked_ring);
                        if (SL_EXPECT_FALSE(unacked->get_reserved_slot(0) == nullptr)) {
                            return; // Yield and retry next temporal tick to prevent sequence leaking
                        }
                    }
                }

                auto* egress = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_egress_conduit);
                outbound_tcp_segment<1460>* slot = egress->get_reserved_slot(0);
                if (slot) {
                    slot->mbuf = rte_pktmbuf_alloc(tcb.tx_mbuf_pool);
                    if (slot->mbuf) {
                        uint32_t p_len = tcb.control_payload_len;
                        tcb.control_payload_len = 0;
                        
                        // CRITICAL FIX: Axiom 28 - Spatial Information Leakage in Zero-Window Persistence Posets
                        if (tcb.is_ipv6) {
                            auto* hdr6 = reinterpret_cast<raw_tcp_ipv6_frame*>(slot->get_header());
                            std::memset(hdr6, 0, sizeof(raw_tcp_ipv6_frame) + p_len);
                        } else {
                            std::memset(slot->get_header(), 0, sizeof(raw_tcp_ipv4_frame) + p_len);
                        }
                        
                        prepare_outbound(tcb, *slot, p_len, tcb.flags_pending);
                        
                        // Reliable Control Frame Tracking (SYN / FIN must be retransmitted if lost)
                        if (SL_EXPECT_FALSE(tcb.flags_pending & (FLAG_SYN | FLAG_FIN))) {
                            if (tcb.tx_unacked_ring) {
                                auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_unacked_ring);

                                outbound_tcp_segment<1460>* unacked_slot = unacked->get_reserved_slot(0);
                                if (unacked_slot) {
                                    unacked_slot->mbuf = slot->mbuf;
                                    if (SL_EXPECT_FALSE(!unacked_slot->retain())) {
                                        tcb.phase_mask = PHASE_CLOSED;
                                        tcb.flags_pending |= FLAG_RST;
                                    } else {
                                        unacked->commit_n(1);
                                    }
                                }
                            }
                        }
                        
                        egress->commit_n(1);
                    }
                }
                tcb.flags_pending = 0;
            }
        }
    };

} // namespace slabflux::net