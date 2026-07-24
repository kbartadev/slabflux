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
 * ============================================================================* @file tcp_temporal_wheel.hpp
 * @brief Replaces the background retransmit timer with a holistic temporal orchestrator.
 */

#pragma once

#include <cstdint>
#include "slabflux/net/tcp_transmission_control_block.hpp"
#include "slabflux/net/tcp_retransmit_timer.hpp" // Utilizing rto_tracker math

namespace slabflux::net {

    class tcp_temporal_wheel {
    public:
        /**
         * @brief Evaluates all time-based TCP semantics inline.
         * @details Injected into the `matrix_nexus::poll_and_execute` mesh, ensuring
         * zero latency overhead and absolute L1 cache adjacency.
         */
        static SLAB_HOT void on_tick(uint64_t current_time_ms, 
                                     tcp_transmission_control_block& tcb, 
                                     rto_tracker& tracker) noexcept {
                                         
            if (SL_EXPECT_FALSE(tcb.phase_mask == PHASE_CLOSED)) return;

            // 1. Retransmission Timeout (RTO) - Detection of Contention Collapse
            if (tcb.snd_una != tcb.snd_nxt) {
                tcb.idle_start_ms = 0; // Flight is active, clear idle decay timer
                
                if (SL_EXPECT_FALSE(tcb.rto_deadline_tsc == 0)) {
                    // Auto-arm RTO safely bounded by Karn's multiplier and RTO_MIN floors
                    tcb.rto_deadline_tsc = current_time_ms + tcp_retransmit_timer::extract_rto_boundary(tcb); 
                } else if (current_time_ms >= tcb.rto_deadline_tsc) {
                    if (SL_EXPECT_FALSE(tcb.rto_retries >= 15)) {
                        // RTO Exhaustion (RFC 1122): Remote peer vanished. Abort connection.
                        tcb.phase_mask = PHASE_CLOSED;
                        tcb.flags_pending |= FLAG_RST;
                        return;
                    }
                    tcb.rto_retries++;

                    // AIMD Slow-Start Reset
                    tcb.ssthresh = (tcb.cwnd >> 1) > 2920u ? (tcb.cwnd >> 1) : 2920u;
                    tcb.cwnd = 1460u;
                    tcb.dup_acks = 0;
                    
                    // Exponential Backoff (Capped natively via extract_rto_boundary's Karn shift)
                    tcb.rto_deadline_tsc = current_time_ms + tcp_retransmit_timer::extract_rto_boundary(tcb);
                    tcb.temporal_flags |= TEMP_FLAG_RETRANSMIT;

                    // Direct Active Retransmission (RFC 5681: Retransmit FIRST unacknowledged segment)
                    if (tcb.tx_unacked_ring && tcb.tx_egress_conduit) {
                        auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_unacked_ring);
                        auto* egress = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_egress_conduit);
                        
                        // RFC 2018: Clear SACK flags on RTO
                        for (size_t j = 0; j < unacked->available_to_peek(); ++j) {
                            const_cast<outbound_tcp_segment<1460>*>(unacked->get_peek_slot(j))->is_sacked = false;
                        }

                        if (unacked->available_to_peek() > 0) {
                            const outbound_tcp_segment<1460>* lost_frame = unacked->get_peek_slot(0);
                            outbound_tcp_segment<1460>* slot = egress->get_reserved_slot(0);
                            if (slot) {
                                slot->mbuf = lost_frame->mbuf;
                                if (SL_EXPECT_FALSE(!slot->retain())) {
                                    tcb.phase_mask = PHASE_CLOSED;
                                    tcb.flags_pending |= FLAG_RST;
                                    return;
                                }

                                slot->get_header()->tcp_ack = core::endian::host_to_network32(tcb.rcv_nxt);
                                uint32_t wnd = tcb.rcv_wnd >> tcb.rcv_wscale;
                                slot->get_header()->tcp_window = core::endian::host_to_network16(wnd > 65535 ? 65535 : wnd);

                                auto* rtx_hdr = slot->get_header();
                                rtx_hdr->ip_checksum = 0;
                                rtx_hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(rtx_hdr) + 14, 20, 0);
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
                tcb.rto_deadline_tsc = 0; // Flight is empty, disarm RTO
                
                // CWND Idle Decay (RFC 5681)
                if (tcb.idle_start_ms == 0) {
                    tcb.idle_start_ms = current_time_ms;
                } else if (tcb.cwnd > 1460u * 10 && (current_time_ms - tcb.idle_start_ms) >= std::max<uint32_t>(tracker.rto, 200u)) {
                    tcb.cwnd = 1460u * 10; // Reset to Initial Window (IW10)
                }
            }

            // 2. Delayed ACK Trigger
            if (tcb.temporal_flags & TEMP_FLAG_ACK_PENDING) {
                if (tcb.delayed_ack_deadline_tsc == 0) {
                    tcb.delayed_ack_deadline_tsc = current_time_ms + 200; // 200ms delay default
                } else if (current_time_ms >= tcb.delayed_ack_deadline_tsc) {
                    tcb.temporal_flags &= ~TEMP_FLAG_ACK_PENDING;
                    // Dispatcher will catch this flag and invoke standalone ACK creation
                    tcb.flags_pending |= FLAG_ACK; 
                    tcb.delayed_ack_deadline_tsc = 0;
                }
            } else {
                tcb.delayed_ack_deadline_tsc = 0;
            }

            // 3. Zero-Window Probes (Persistence Timer)
            if (tcb.snd_wnd == 0 && (tcb.snd_una != tcb.snd_nxt || (tcb.temporal_flags & TEMP_FLAG_ZWP_ACTIVE))) {
                if (!(tcb.temporal_flags & TEMP_FLAG_ZWP_ACTIVE) || tcb.persist_deadline_tsc == 0) {
                    tcb.temporal_flags |= TEMP_FLAG_ZWP_ACTIVE;
                    tcb.persist_deadline_tsc = current_time_ms + 1000; // 1s initial ZWP
                    tcb.zwp_retries = 0;
                } else if (current_time_ms >= tcb.persist_deadline_tsc) {
                    if (SL_EXPECT_FALSE(tcb.zwp_retries >= 5)) {
                        // CRITICAL FIX: Axiom 14 - Asymmetric Topological Locking in Zero-Window Posets
                        // Tighter ZWP exhaustion boundary (~25s timeout) to prevent resource locking.
                        tcb.phase_mask = PHASE_CLOSED;
                        tcb.flags_pending |= FLAG_RST;
                        return;
                    }
                    tcb.zwp_retries++;
                    // Force 1 byte send logic via the engine outbound prep
                    tcb.flags_pending |= FLAG_ACK;
                    tcb.control_payload_len = 1; // Send 1 byte of garbage
                    tcb.persist_deadline_tsc = current_time_ms + 5000; // Cap backoff at 5s
                }
            } else {
                tcb.temporal_flags &= ~TEMP_FLAG_ZWP_ACTIVE;
                tcb.zwp_retries = 0;
            }

            // 4. TIME_WAIT 2MSL Teardown Freezing & Orphan Connection Timeout
            if (tcb.phase_mask == PHASE_TIME_WAIT || tcb.phase_mask == PHASE_FIN_WAIT2 || tcb.phase_mask == PHASE_CLOSING) {
                if (tcb.time_wait_deadline_tsc == 0) {
                    // Initialize 2MSL timer (typically 60-120 seconds in standard stacks, we use 60s)
                    tcb.time_wait_deadline_tsc = current_time_ms + 60000; 
                } else if (current_time_ms >= tcb.time_wait_deadline_tsc) {
                    tcb.phase_mask = PHASE_CLOSED; // Fully release the slot
                    tcb.time_wait_deadline_tsc = 0;
                }
            }
            
            // 5. Keep-Alive Probe
            if (tcb.temporal_flags & TEMP_FLAG_KEEPALIVE && tcb.phase_mask == PHASE_ESTABLISHED) {
                if (tcb.snd_una == tcb.snd_nxt) {
                    if (tcb.keepalive_deadline_tsc == 0) {
                        tcb.keepalive_deadline_tsc = current_time_ms + 7200000; // 2 hours
                        tcb.keepalive_retries = 0;
                    } else if (current_time_ms >= tcb.keepalive_deadline_tsc) {
                        if (SL_EXPECT_FALSE(tcb.keepalive_retries >= 9)) {
                            // Keep-Alive Exhaustion: Remote peer is a zombie. Abort connection.
                            tcb.phase_mask = PHASE_CLOSED;
                            tcb.flags_pending |= FLAG_RST;
                            return;
                        }
                        tcb.keepalive_retries++;
                        // Send a garbage ACK to solicit a response
                        tcb.flags_pending |= FLAG_ACK;
                        tcb.temporal_flags |= TEMP_FLAG_KEEPALIVE_PROBE;
                        tcb.keepalive_deadline_tsc = current_time_ms + 75000; // Retry every 75s
                    }
                } else {
                    tcb.keepalive_deadline_tsc = 0; // Reset while active
                    tcb.keepalive_retries = 0;
                }
            }
            
            // 6. ARP Resolution Timeout
            if (tcb.temporal_flags & TEMP_FLAG_ARP_WAIT) {
                if (tcb.rto_deadline_tsc == 0) {
                    tcb.rto_deadline_tsc = current_time_ms + 3000; // 3 seconds to resolve
                } else if (current_time_ms >= tcb.rto_deadline_tsc) {
                    tcb.phase_mask = PHASE_CLOSED; // Timeout, recycle the slot
                    tcb.temporal_flags &= ~TEMP_FLAG_ARP_WAIT;
                    tcb.rto_deadline_tsc = 0;
                    
                    // Axiom 20: Principal Fiber Bundle Exhaustion in Spatial Teardown Vectors
                    if (tcb.tx_unacked_ring) {
                        auto* unacked = static_cast<core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024>*>(tcb.tx_unacked_ring);
                        while (unacked->available_to_peek() > 0) {
                            const_cast<outbound_tcp_segment<1460>*>(unacked->get_peek_slot(0))->release();
                            unacked->consume_n(1);
                        }
                    }
                }
            }
        }
    };

} // namespace slabflux::net