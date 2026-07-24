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
 * ============================================================================* @file tcp_retransmit_timer.hpp
 * @brief Deterministic RTO Tracker & Congestion Matrix.
 */

#pragma once

#include <cstdint>
#include <algorithm>
#include "slabflux/net/tcp_flow_engine.hpp"

namespace slabflux::net {

    /**
     * @brief Overlay structure for RTO tracking.
     * @details Maps directly to the RTT state fields in the TCB for 
     * deterministic temporal updates.
     */
    struct rto_tracker {
        uint64_t srtt;
        uint64_t rttvar;
        uint32_t rto;
    };

    class tcp_congestion_matrix {
    public:
        /**
         * @brief Aliases internal wait-free capacity directly to TCP Receive Window.
         */
        static SLAB_FORCE_INLINE void sync_receive_window(tcp_transmission_control_block& tcb, size_t available_rx_capacity) noexcept {
            tcb.rcv_wnd = (available_rx_capacity > 65535) ? 65535 : static_cast<uint32_t>(available_rx_capacity);
        }
    };

    class tcp_retransmit_timer {
    private:
        // Utilize the upper 3 bits of temporal_flags (0x20, 0x40, 0x80) for Karn's multiplier (0-7)
        static constexpr uint8_t KARN_BACKOFF_MASK = 0xE0;
        static constexpr uint8_t KARN_BACKOFF_SHIFT = 5;

        // 200ms Minimum RTO floor normalized to strictly millisecond time boundaries
        static constexpr uint64_t RTO_MIN_MS = 200ULL;
        
        // --- CONFIGURATION: OTSP Manifold kikapcsolva, Karn's/Jacobson aktív ---
        static constexpr bool USE_OTSP_MANIFOLD = false;

    public:
        /**
         * @brief RTT becslés - Karn's Rule 1 érvényesítése Jacobson/Karels mellett.
         */
        static SLAB_FORCE_INLINE void record_coordinate(tcp_transmission_control_block& tcb, uint64_t t_tx, uint64_t t_rx, uint32_t seq_rx) noexcept {
            if constexpr (USE_OTSP_MANIFOLD) {
                const uint64_t delta_t = t_rx - t_tx;

                // 1. Manifold Floor (Branchless Min)
                const uint64_t is_new_base = (delta_t < tcb.otsp_t_base) ? ~0ULL : 0ULL;
                tcb.otsp_t_base = (delta_t & is_new_base) | (tcb.otsp_t_base & ~is_new_base);

                // 2. Spatial Stretch
                const uint64_t stretch = delta_t - tcb.otsp_t_base;

                // 3. Phase Evaluation (64KB Spatial Grid)
                const uint32_t new_epoch = seq_rx >> 16;
                const uint64_t epoch_crossed = (new_epoch != tcb.otsp_epoch) ? ~0ULL : 0ULL;

                // 4. Matrix Rotation (Branchless bitmask)
                tcb.otsp_d_shadow = (tcb.otsp_d_active & epoch_crossed) | (tcb.otsp_d_shadow & ~epoch_crossed);
                tcb.otsp_d_active = (0ULL & epoch_crossed) | (tcb.otsp_d_active & ~epoch_crossed);
                tcb.otsp_epoch = new_epoch;

                // 5. Dilation Expansion (Branchless Max)
                const uint64_t is_greater = (stretch > tcb.otsp_d_active) ? ~0ULL : 0ULL;
                tcb.otsp_d_active = (stretch & is_greater) | (tcb.otsp_d_active & ~is_greater);
            } else {
                // --- KARN'S ALGORITHM RULE 1 ---
                // Retranszmitált szegmensből nem veszünk mintát
                if (SL_EXPECT_FALSE(tcb.temporal_flags & TEMP_FLAG_RETRANSMIT)) return;

                const uint64_t delta_t = t_rx - t_tx;
                if (SL_EXPECT_FALSE(tcb.srtt == 0)) {
                    tcb.srtt = delta_t;
                    tcb.rttvar = delta_t >> 1;
                } else {
                    // Jacobson/Karels IIR: g=1/8, h=1/4
                    int64_t err = static_cast<int64_t>(delta_t) - static_cast<int64_t>(tcb.srtt);
                    tcb.srtt += (err >> 3);
                    uint64_t abs_err = (err < 0) ? -err : err;
                    int64_t var_err = static_cast<int64_t>(abs_err) - static_cast<int64_t>(tcb.rttvar);
                    tcb.rttvar += (var_err >> 2);
                }
            }
        }

        /**
         * @brief Math-only RTT update for unit testing and state prediction.
         * @details Directly updates the srtt, rttvar, and rto fields within an rto_tracker.
         */
        static SLAB_FORCE_INLINE void record_ack_rtt(rto_tracker& tracker, uint64_t delta_t) noexcept {
            if (SL_EXPECT_FALSE(tracker.srtt == 0)) {
                tracker.srtt = delta_t;
                tracker.rttvar = delta_t >> 1;
            } else {
                // Jacobson/Karels IIR: g=1/8, h=1/4
                int64_t err = static_cast<int64_t>(delta_t) - static_cast<int64_t>(tracker.srtt);
                tracker.srtt += (err >> 3);
                uint64_t abs_err = (err < 0) ? -err : err;
                int64_t var_err = static_cast<int64_t>(abs_err) - static_cast<int64_t>(tracker.rttvar);
                tracker.rttvar += (var_err >> 2);
            }
            // Recalculate RTO based on the new RTT coordinates
            tracker.rto = static_cast<uint32_t>(tracker.srtt + (tracker.rttvar << 2));
        }

        /**
         * @brief RTO határ kiszámítása - Karn's Rule 2 (Exponential Backoff) érvényesítése.
         */
        static SLAB_FORCE_INLINE uint64_t extract_rto_boundary(const tcp_transmission_control_block& tcb) noexcept {
            uint64_t rto;
            if constexpr (USE_OTSP_MANIFOLD) {
                const uint64_t d_cmp = (tcb.otsp_d_active > tcb.otsp_d_shadow) ? ~0ULL : 0ULL;
                const uint64_t max_d = (tcb.otsp_d_active & d_cmp) | (tcb.otsp_d_shadow & ~d_cmp);
                
                // Fallback base of 1 second (Normalized to MS) to strictly prevent initial storm
                uint64_t base = (tcb.otsp_t_base == 0xFFFFFFFFFFFFFFFFULL) ? 1000ULL : tcb.otsp_t_base;
                
                uint64_t rto = base + max_d + 2ULL; // Geometric Box + 2ms micro-architectural pad
                
                if (rto < RTO_MIN_MS) {
                    rto = RTO_MIN_MS;
                }

                // Exponential backoff must universally persist to protect network integrity
                uint8_t shift = (tcb.temporal_flags & KARN_BACKOFF_MASK) >> KARN_BACKOFF_SHIFT;
                rto <<= shift;
            } else {
                if (SL_EXPECT_FALSE(tcb.srtt == 0)) return 1000ULL; // 1s initial
                rto = tcb.srtt + (tcb.rttvar << 2);
            }

            if (rto < RTO_MIN_MS) rto = RTO_MIN_MS;

            // --- KARN'S ALGORITHM RULE 2 ---
            // Perzisztens exponenciális visszalépés a rto_retries számláló alapján
            return rto << std::min(static_cast<uint32_t>(tcb.rto_retries), 6u);
        }

        /**
         * @brief Evaluates temporal progression for expired TX segments.
         */
        static SLAB_HOT void on_temporal_tick(uint64_t current_time_tsc, tcp_transmission_control_block& tcb) noexcept {
            if (SL_EXPECT_FALSE(tcb.phase_mask == PHASE_CLOSED || tcb.phase_mask == PHASE_LISTEN)) return;
            
            if (tcb.snd_una != tcb.snd_nxt) {
                // Auto-arm the timer if it was reset by an ACK or initial transmission
                if (SL_EXPECT_FALSE(tcb.rto_deadline_tsc == 0)) {
                    tcb.rto_deadline_tsc = current_time_tsc + extract_rto_boundary(tcb);
                    return;
                }

                // Detection of Contention Collapse (RTO Expiration)
                if (SL_EXPECT_FALSE(current_time_tsc > tcb.rto_deadline_tsc)) {
                    
                    // Connection Halt Detection: Abort after 15 consecutive timeouts (~120s of silence)
                    if (SL_EXPECT_FALSE(tcb.rto_retries >= 15)) {
                        tcb.phase_mask = PHASE_CLOSED; // Sever the connection natively
                        tcb.flags_pending |= FLAG_RST; // Dispatch a physical RST frame to the wire
                        return;
                    }
                    tcb.rto_retries = std::min(tcb.rto_retries + 1, 15); // Cap retries

                    // AIMD Slow-Start Reset
                    // CRITICAL FIX: Axiom 28 - Lexicographical Underflow in Congestion Metric Scaling
                    uint32_t safe_cwnd = (tcb.cwnd >= 1460u) ? tcb.cwnd : 1460u;
                    tcb.ssthresh = (safe_cwnd >> 1) > 2920u ? (safe_cwnd >> 1) : 2920u;                    tcb.cwnd = 1460u; // Mathematical window fallback
                    tcb.dup_acks = 0;

                    // Extract RTO with the now-incremented backoff multiplier
                    uint64_t next_rto = extract_rto_boundary(tcb);

                    tcb.rto_deadline_tsc = current_time_tsc + std::min<uint64_t>(next_rto, 180000000000ULL);
                    
                    // Pure wait-free retransmission via horizon rollback.
                    tcb.snd_nxt = tcb.snd_una;
                    tcb.temporal_flags |= TEMP_FLAG_RETRANSMIT;
                }
            } else {
                // Flight is empty, disarm the timer completely
                tcb.rto_deadline_tsc = 0;
            }
        }
    };

} // namespace slabflux::net