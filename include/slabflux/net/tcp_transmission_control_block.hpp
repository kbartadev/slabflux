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
struct rte_mempool;

namespace slabflux::net {

    // Flattened RFC 793 State Machine
    enum tcp_phase_bits : uint16_t {
        PHASE_CLOSED       = 0x00,
        PHASE_LISTEN       = 0x01,
        PHASE_SYN_SENT     = 0x02,
        PHASE_SYN_RCVD     = 0x04,
        PHASE_ESTABLISHED  = 0x08,
        PHASE_FIN_WAIT1    = 0x10,
        PHASE_FIN_WAIT2    = 0x20,
        PHASE_TIME_WAIT    = 0x40,
        PHASE_CLOSE_WAIT   = 0x80,
        PHASE_LAST_ACK     = 0x0100,
        PHASE_CLOSING      = 0x0200
    };

    enum tcp_temporal_flags : uint8_t {
        TEMP_FLAG_NONE        = 0x00,
        TEMP_FLAG_ACK_PENDING = 0x01, // Delayed ACK armed
        TEMP_FLAG_ZWP_ACTIVE  = 0x02, // Persistence mode
        TEMP_FLAG_KEEPALIVE   = 0x04, // Idle probe active
        TEMP_FLAG_ARP_WAIT    = 0x08, // Awaiting Layer 2 Resolution
        TEMP_FLAG_RETRANSMIT  = 0x10, // Karn's Algorithm Guard
        TEMP_FLAG_KEEPALIVE_PROBE = 0x20, // Explicit RFC 1122 (snd_nxt - 1)
        TEMP_FLAG_ECN_ECHO    = 0x40, // Needs to send ECE
        TEMP_FLAG_CWR         = 0x80  // Needs to send CWR
    };

    // Static 192-byte Connection Geometry (Zero dynamic allocation, perfectly tiles into 3 L1 Cache Lines)
    struct alignas(64) tcp_transmission_control_block {
        // --- CACHE LINE 1: Hot-Path Flow Control & Sequencing (64 Bytes) ---
        uint32_t snd_una{0};          // Send Unacknowledged
        uint32_t snd_nxt{0};          // Send Next
        uint32_t rcv_nxt{0};          // Receive Next
        uint32_t snd_wnd{0};          // Peer's Advertised Window
        uint32_t rcv_wnd{0};          // Local Window (Aliased to Conduit Capacity)
        uint32_t cwnd{0};             // Congestion Window (Contention Tracking)
        uint32_t ssthresh{0};         // Slow Start Threshold

        uint32_t remote_ipv4{0};
        uint32_t local_ipv4{0};
        uint16_t remote_port{0};
        uint16_t local_port{0};

        uint16_t phase_mask{0};       // 16-bit deterministic state mask
        uint16_t remote_mss{0};       // Path MTU Clamping
        uint8_t  dup_acks{0};         // Fast Retransmit anomaly counter
        uint8_t  snd_wscale{0};       // RFC 7323 Window Scaling
        uint8_t  rcv_wscale{0};       // Local Window Scaling
        uint8_t  temporal_flags{0};   // Bitmask for delayed ack, persist, etc.

        uint64_t rto_deadline_tsc{0}; // Cycles deadline for synchronous temporal tick
        uint8_t  flags_pending{0};    // Pending control flags (e.g., standalone ACK)
        uint8_t  rto_retries{0};
        uint8_t  zwp_retries{0};   // Zero-Window Probe backoff counter
        uint8_t  sack_permitted{0}; // RFC 2018 TCP Selective Acknowledgements
        uint8_t  ts_permitted{0};  // RFC 7323 Timestamps & PAWS
        uint8_t  keepalive_retries{0}; // Zombie connection tracker
        uint8_t  ecn_permitted{0}; // RFC 3168 Explicit Congestion Notification
        uint8_t  control_payload_len{0}; // For 1-byte ZWP / Keep-Alive

        // --- CACHE LINE 2: Retransmission Math & Substrate Conduits (64 Bytes) ---
        void* rx_stream_ring{nullptr};      // Downstream defragmenter target
        void* tx_egress_conduit{nullptr};   // Volatile physical NIC injection wire
        void* tx_unacked_ring{nullptr};     // Standard TCP sliding window buffer
        struct rte_mempool* tx_mbuf_pool{nullptr}; // DPDK Mempool for Zero-Copy Egress
        void* ooo_matrix{nullptr};          // Spatial Out-Of-Order Buffer Ring
        uint8_t target_mac[6];     // ARP resolved destination MAC
        uint8_t expected_ttl{0};   // GFW / DPI TTL Hop-Count baseline
        uint8_t challenge_ack_cnt{0}; // RFC 5961 Amplification Defense
        uint64_t last_challenge_tsc{0}; // Monotonic boundary for challenge ACKs
        uint32_t highest_rtx_seq{0}; // Tracks highest retransmitted coordinate plane
        uint8_t _pad_cl2[4]{0};

        // --- CACHE LINE 3: Extended Temporal & Spatial Horizons (64 Bytes) ---
        uint64_t delayed_ack_deadline_tsc{0}; // Wait 200ms before sending standalone ACK
        uint64_t persist_deadline_tsc{0};     // Zero-Window Probe backoff timer
        uint64_t time_wait_deadline_tsc{0};   // 2MSL teardown freezing
        uint64_t keepalive_deadline_tsc{0};   // Idle connection probe timer

        uint64_t srtt{0};
        uint64_t rttvar{0};
        uint32_t rto{0};
        uint32_t _pad_cl4{0};

        uint64_t otsp_t_base{0};

        // --- CACHE LINE 4: Extended Horizons (64 Bytes) ---
        uint64_t otsp_d_shadow{0};
        uint64_t otsp_d_active{0};
        uint32_t otsp_epoch{0};
        uint32_t recover{0};       // RFC 6582 NewReno Fast Recovery horizon
        uint32_t ts_recent{0};     // PAWS Rejection Clock
        uint32_t ts_val{0};        // Local Monotonic Clock
        uint32_t sack_left_edges[3]{0}; // MRU SACK blocks (RFC 2018)
        uint32_t sack_right_edges[3]{0};
        bool     sack_valid[3]{false};  // Fix for Sequence 0 Sentinel Vulnerability
        uint8_t  quick_ack_cnt{0}; // RFC 5681 Quick ACK trigger
        uint32_t idle_start_ms{0}; // RFC 5681 CWND Idle Burst protection

        // --- CACHE LINE 5: IPv6 Extended Coordinates (64 Bytes) ---
        uint64_t remote_ipv6[2]{0, 0};
        uint64_t local_ipv6[2]{0, 0};
        uint8_t  is_ipv6{0};
        uint8_t  _pad_cl5[31]{0};
    };

    static_assert(sizeof(tcp_transmission_control_block) == 320, "TCB must exactly occupy five cache lines.");

} // namespace slabflux::net