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
 * ============================================================================* @file tcp_flow_test.cpp
 * @brief Strict validation of the SlabFlux OS-Bypass TCP Flow Engine.
 */

#include <gtest/gtest.h>
#include <cstring>

#include <rte_mbuf.h>

// Mock DPDK Mempool Allocations for Unit Testing
inline struct rte_mbuf* mock_mbuf_alloc_flow(struct rte_mempool*) {
    char* mem = new char[sizeof(struct rte_mbuf) + 2048];
    std::memset(mem, 0, sizeof(struct rte_mbuf) + 2048);
    struct rte_mbuf* m = reinterpret_cast<struct rte_mbuf*>(mem);
    m->buf_addr = mem + sizeof(struct rte_mbuf);
    m->data_off = 0;
    m->pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    rte_mbuf_refcnt_set(m, 1);
    return m;
}

inline void mock_mbuf_free_flow(struct rte_mbuf* m) {
    if (m) {
        if (rte_mbuf_refcnt_read(m) <= 1) delete[] reinterpret_cast<char*>(m);
        else rte_mbuf_refcnt_update(m, -1);
    }
}

#define rte_pktmbuf_alloc mock_mbuf_alloc_flow
#define rte_pktmbuf_free mock_mbuf_free_flow

#include "slabflux/net/tcp_flow_engine.hpp"
#include "slabflux/net/virtual_tcp_socket.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"
#include "slabflux/net/tcp_retransmit_timer.hpp"
#include "slabflux/transport/http_avx.hpp"
#include "slabflux/net/tcp_wire_engine.hpp"
#include "slabflux/net/network_conduit_virtual.hpp"
#include "slabflux/net/virtual_tcp_acceptor.hpp"

using namespace slabflux::net;

TEST(TcpWireEngineTest, BranchlessChecksumValidation) {
    // Validate that the vectorized 1's complement math is correct
    uint32_t pseudo_sum = 0x1234;
    uint32_t payload[4] = { 0x11111111, 0x22222222, 0x33333333, 0x44444444 };
    
    uint16_t csum = tcp_wire_engine::compute_checksum(payload, sizeof(payload), pseudo_sum);
    
    // Ensure it executed and didn't zero out erroneously 
    EXPECT_NE(csum, 0); 
}

TEST(TcpFlowEngineTest, RFC_ThreeWayHandshake) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_LISTEN;
    tcb.rcv_wnd = 65535;

    // 1. Remote initiates connection (SYN)
    raw_tcp_ipv4_frame syn_frame{};
    syn_frame.tcp_flags = FLAG_SYN;
    syn_frame.tcp_seq = slabflux::core::endian::host_to_network32(1000);
    
    uint32_t p_len = 0;
    bool accepted = tcp_flow_engine::process_inbound(tcb, syn_frame, p_len);
    ASSERT_TRUE(accepted);
    
    // The TCB must map perfectly to SYN_RCVD via bitwise logic
    EXPECT_EQ(tcb.phase_mask, PHASE_SYN_RCVD);
    // SYN physically consumes 1 mathematical sequence number
    EXPECT_EQ(tcb.rcv_nxt, 1001); 

    // 2. Gateway prepares outbound SYN-ACK
    tcb.snd_nxt = 5000;
    alignas(64) char syn_ack_buffer[128]{0};
    auto& syn_ack_frame = *reinterpret_cast<raw_tcp_ipv4_frame*>(syn_ack_buffer);
    tcp_flow_engine::prepare_outbound_header(tcb, syn_ack_frame, 0, FLAG_SYN);
    
    EXPECT_EQ(syn_ack_frame.tcp_flags, FLAG_SYN | FLAG_ACK);
    EXPECT_EQ(syn_ack_frame.tcp_seq, slabflux::core::endian::host_to_network32(5000));
    EXPECT_EQ(syn_ack_frame.tcp_ack, slabflux::core::endian::host_to_network32(1001)); // Acknowledges remote SYN
    EXPECT_EQ(tcb.snd_nxt, 5001); // Local SYN sequence consumed

    // 3. Remote completes handshake (ACK)
    raw_tcp_ipv4_frame ack_frame{};
    ack_frame.tcp_flags = FLAG_ACK;
    ack_frame.tcp_seq = slabflux::core::endian::host_to_network32(1001);
    ack_frame.tcp_ack = slabflux::core::endian::host_to_network32(5001);
    ack_frame.tcp_window = slabflux::core::endian::host_to_network16(16384);
    
    p_len = 0;
    accepted = tcp_flow_engine::process_inbound(tcb, ack_frame, p_len);
    ASSERT_TRUE(accepted);
    
    // Connection is fully ESTABLISHED and ready for the Virtual Socket
    EXPECT_EQ(tcb.phase_mask, PHASE_ESTABLISHED);
    EXPECT_EQ(tcb.snd_una, 5001); // ACK boundary advanced
    EXPECT_EQ(tcb.snd_wnd, 16384); // Sliding window dynamically synced
}

TEST(TcpFlowEngineTest, DataTransferAndWindowSlide) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.rcv_nxt = 1001;
    tcb.rcv_wnd = 65535;

    // Receive an in-order data payload
    raw_tcp_ipv4_frame data_frame{};
    data_frame.tcp_flags = FLAG_ACK | FLAG_PSH;
    data_frame.tcp_seq = slabflux::core::endian::host_to_network32(1001); // Matches rcv_nxt perfectly
    
    uint32_t p_len = 150;
    bool accepted = tcp_flow_engine::process_inbound(tcb, data_frame, p_len);
    ASSERT_TRUE(accepted);
    
    // RCV horizon correctly advanced by exactly the payload length
    EXPECT_EQ(tcb.rcv_nxt, 1151); 
}

TEST(TcpFlowEngineTest, RetransmissionRejection) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.rcv_nxt = 5000;
    tcb.rcv_wnd = 1000;

    // Remote mistakenly retransmits a sequence deeply outside the RCV window
    raw_tcp_ipv4_frame bad_frame{};
    bad_frame.tcp_flags = FLAG_ACK | FLAG_PSH;
    bad_frame.tcp_seq = slabflux::core::endian::host_to_network32(2000); 
    
    uint32_t p_len = 150;
    bool accepted = tcp_flow_engine::process_inbound(tcb, bad_frame, p_len);
    
    // The engine must reject the payload but ACCEPT the frame to generate a Challenge ACK
    EXPECT_TRUE(accepted);
    EXPECT_EQ(p_len, 0); // Payload dropped
    EXPECT_TRUE(tcb.flags_pending & FLAG_ACK); // ACK queued
    EXPECT_EQ(tcb.rcv_nxt, 5000); // State immutably preserved
}

TEST(TcpFlowEngineTest, PureWindowUpdate) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.rcv_nxt = 5000;
    tcb.snd_una = 2000;
    tcb.snd_nxt = 3000; // 1000 bytes currently in flight
    tcb.snd_wnd = 4096; // Old window size
    tcb.snd_wscale = 0;
    tcb.rcv_wnd = 65535;

    // A pure window update acknowledges no new data (tcp_ack == snd_una)
    // but advertises a newly available receive window capacity.
    raw_tcp_ipv4_frame win_update{};
    win_update.tcp_flags = FLAG_ACK;
    win_update.tcp_seq = slabflux::core::endian::host_to_network32(5000); // Matches rcv_nxt
    win_update.tcp_ack = slabflux::core::endian::host_to_network32(2000); // Matches snd_una
    win_update.tcp_window = slabflux::core::endian::host_to_network16(8192); // Increased window
    
    uint32_t p_len = 0;
    uint32_t trim_offset = 0;
    bool accepted = tcp_flow_engine::process_inbound(tcb, win_update, p_len, trim_offset);
    
    ASSERT_TRUE(accepted);
    EXPECT_EQ(tcb.snd_wnd, 8192); // Window strictly updated
    EXPECT_EQ(tcb.dup_acks, 0);   // MUST NOT be classified as a duplicate ACK!
}

TEST(TcpFlowEngineTest, AckStormPrevention) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.rcv_nxt = 5000;
    tcb.rcv_wnd = 1000;
    tcb.snd_nxt = 2000;
    tcb.snd_una = 2000;
    tcb.flags_pending = 0;

    // Craft a pure ACK from a peer that is completely out of sequence sync
    // Its sequence number (15000) is far beyond our acceptable rcv_nxt + rcv_wnd (6000)
    raw_tcp_ipv4_frame spoofed_ack{};
    spoofed_ack.tcp_flags = FLAG_ACK;
    spoofed_ack.tcp_seq = slabflux::core::endian::host_to_network32(15000); 
    spoofed_ack.tcp_ack = slabflux::core::endian::host_to_network32(2000);
    
    uint32_t p_len = 0;
    uint32_t trim_offset = 0;
    bool accepted = tcp_flow_engine::process_inbound(tcb, spoofed_ack, p_len, trim_offset);
    
    // The engine MUST silently drop the packet to sever the ping-pong loop.
    // It must NOT queue a Challenge ACK!
    EXPECT_FALSE(accepted);
    EXPECT_EQ(tcb.flags_pending & FLAG_ACK, 0);
}

TEST(VirtualTcpSocketTest, ZeroAllocationSendAndBufferRing) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.snd_wnd = 4096; // 4KB remote window
    tcb.snd_nxt = 100;
    tcb.snd_una = 100;
    tcb.rcv_nxt = 500;
    tcb.cwnd = 14600;
    tcb.tx_mbuf_pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF); // Bypasses nullptr check

    // Physically wire the Virtual Socket to the wait-free transmission conduit
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> unacked_ring;
    tcb.tx_egress_conduit = &tx_ring;
    tcb.tx_unacked_ring = &unacked_ring;

    virtual_tcp_socket sock(&tcb);
    
    const char* payload = "Hello SlabFlux!";
    ssize_t sent = sock.send(payload, 15);
    
    EXPECT_EQ(sent, 15);
    EXPECT_EQ(tcb.snd_nxt, 115); // Sequence consumed
    
    // Validate that the raw frame was perfectly queued for the Matrix Nexus
    EXPECT_EQ(tx_ring.available_to_peek(), 1);
    const outbound_tcp_segment<1460>* segment = tx_ring.get_peek_slot(0);
    
    // TCP Math validation
    EXPECT_EQ(segment->get_header()->tcp_seq, slabflux::core::endian::host_to_network32(100));
    EXPECT_EQ(segment->get_header()->tcp_ack, slabflux::core::endian::host_to_network32(500));
    EXPECT_EQ(segment->get_header()->tcp_flags, FLAG_PSH | FLAG_ACK);
    
    // Spatial integrity verification (Payload seamlessly attached to header)
    EXPECT_EQ(std::strncmp(segment->get_payload(), payload, 15), 0);
}

TEST(VirtualTcpSocketTest, RFC_ConnectionTeardown) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    
    virtual_tcp_socket sock(&tcb);
    sock.close(); // User-space application triggers Active Close
    
    // Must immediately jump to FIN_WAIT1
    EXPECT_EQ(tcb.phase_mask, PHASE_FIN_WAIT1);
    
    // Subsequent sends must be mathematically bounded to fail
    ssize_t blocked_send = sock.send("dead bytes", 10);
    EXPECT_EQ(blocked_send, -1);
}

TEST(VirtualTcpSocketTest, ZeroWindowBackpressure) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.snd_wnd = 50; // Remote window is artificially tiny
    tcb.snd_nxt = 100;
    tcb.snd_una = 100;
    tcb.cwnd = 14600;
    tcb.tx_mbuf_pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);

    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> unacked_ring;
    tcb.tx_egress_conduit = &tx_ring;
    tcb.tx_unacked_ring = &unacked_ring;

    virtual_tcp_socket sock(&tcb);
    
    const char* payload = "This payload is significantly larger than 50 bytes, triggering window exhaustion.";
    size_t len = std::strlen(payload);

    // First send should perfectly saturate the advertised window
    ssize_t sent1 = sock.send(payload, len);
    EXPECT_EQ(sent1, 50); 
    EXPECT_EQ(tcb.snd_nxt, 150);

    // Immediate subsequent send must trigger Zero-Window backpressure natively
    ssize_t sent2 = sock.send(payload + sent1, len - sent1);
    EXPECT_EQ(sent2, 0); // 0 = EWOULDBLOCK equivalence
    EXPECT_EQ(tcb.snd_nxt, 150); // State mutation must be blocked
}

TEST(TcpFlowEngineTest, AIMD_CongestionControl_Math) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.cwnd = 14600; // IW10
    tcb.ssthresh = 65535;
    tcb.snd_una = 1000;
    tcb.snd_nxt = 2460; // 1 MSS in flight
    tcb.rcv_nxt = 5000;
    tcb.rcv_wnd = 65535;
    
    // 1. Validate Slow Start (cwnd < ssthresh)
    raw_tcp_ipv4_frame ack_frame{};
    ack_frame.tcp_flags = FLAG_ACK;
    ack_frame.tcp_seq = slabflux::core::endian::host_to_network32(5000);
    ack_frame.tcp_ack = slabflux::core::endian::host_to_network32(2460);
    
    uint32_t p_len = 0;
    tcp_flow_engine::process_inbound(tcb, ack_frame, p_len);
    EXPECT_EQ(tcb.cwnd, 14600 + 1460); // Grew by exactly 1 MSS
    
    // 2. Validate Congestion Avoidance (cwnd >= ssthresh)
    tcb.ssthresh = 14600;
    tcb.snd_una = 2460;
    tcb.snd_nxt = 3920;
    ack_frame.tcp_ack = slabflux::core::endian::host_to_network32(3920);
    
    p_len = 0;
    tcp_flow_engine::process_inbound(tcb, ack_frame, p_len);
    EXPECT_LT(tcb.cwnd, 16060 + 1460); // Grew by fraction, not full MSS
}

TEST(TcpFlowEngineTest, FastRetransmit_Anomaly) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.cwnd = 30000;
    tcb.snd_una = 1000;
    tcb.snd_nxt = 8000; 
    tcb.rcv_nxt = 5000;
    tcb.rcv_wnd = 65535;
    
    raw_tcp_ipv4_frame dup_ack{};
    dup_ack.tcp_flags = FLAG_ACK;
    dup_ack.tcp_seq = slabflux::core::endian::host_to_network32(5000);
    dup_ack.tcp_ack = slabflux::core::endian::host_to_network32(1000); // Duplicate!
    
    uint32_t p_len = 0;
    tcp_flow_engine::process_inbound(tcb, dup_ack, p_len);
    tcp_flow_engine::process_inbound(tcb, dup_ack, p_len);
    tcp_flow_engine::process_inbound(tcb, dup_ack, p_len); // 3rd DUP
    
    EXPECT_EQ(tcb.ssthresh, 15000); // Halved
    EXPECT_EQ(tcb.snd_nxt, 8000);   // snd_nxt is NOT rewound during Fast Retransmit!
    EXPECT_EQ(tcb.recover, 8000);   // NewReno Horizon
}

TEST(VirtualTcpSocketTest, MssFragmentation) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.snd_wnd = 65535; // Large window allowing burst transmission
    tcb.snd_nxt = 1000;
    tcb.snd_una = 1000;
    tcb.cwnd = 14600;
    tcb.tx_mbuf_pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);

    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> unacked_ring;
    tcb.tx_egress_conduit = &tx_ring;
    tcb.tx_unacked_ring = &unacked_ring;

    virtual_tcp_socket sock(&tcb);
    
    // Generate 4000 byte block representing a JSON array or image response
    std::string payload(4000, 'A'); 
    
    ssize_t sent = sock.send(payload.data(), payload.size());
    EXPECT_EQ(sent, 4000);
    EXPECT_EQ(tcb.snd_nxt, 5000); // 1000 + 4000

    // Verify that the Fragmenter natively sliced it into exactly 3 Ethernet MSS frames
    EXPECT_EQ(tx_ring.available_to_peek(), 3); // 1460, 1460, 1080

    auto* seg1 = tx_ring.get_peek_slot(0);
    EXPECT_EQ(seg1->get_header()->tcp_seq, slabflux::core::endian::host_to_network32(1000));
    
    auto* seg2 = tx_ring.get_peek_slot(1);
    EXPECT_EQ(seg2->get_header()->tcp_seq, slabflux::core::endian::host_to_network32(2460)); // 1000 + 1460
    
    auto* seg3 = tx_ring.get_peek_slot(2);
    EXPECT_EQ(seg3->get_header()->tcp_seq, slabflux::core::endian::host_to_network32(3920)); // 2460 + 1460
}

TEST(TcpRetransmitTimerTest, RttVarianceCalculation) {
    rto_tracker tracker{};
    
    // Initial measurement (e.g. from 3-Way Handshake SYN-ACK)
    tcp_retransmit_timer::record_ack_rtt(tracker, 100);
    EXPECT_EQ(tracker.srtt, 100);
    EXPECT_EQ(tracker.rttvar, 50); // Initial variance is 1/2 of RTT
    EXPECT_EQ(tracker.rto, 300);   // RTO = SRTT + max(4, 4*RTTVAR) -> 100 + 200

    // Subsequent stable measurement dampens variance
    tcp_retransmit_timer::record_ack_rtt(tracker, 100);
    EXPECT_EQ(tracker.srtt, 100);
    EXPECT_LT(tracker.rttvar, 50); // Variance decreases
    EXPECT_LT(tracker.rto, 300);   // RTO tightens
    
    // Sudden latency spike triggers RTO compensation
    tcp_retransmit_timer::record_ack_rtt(tracker, 500);
    EXPECT_GT(tracker.srtt, 100);  
    EXPECT_GT(tracker.rttvar, 30); 
}

TEST(VirtualTcpSocketTest, HttpApplicationIntegration) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    
    slabflux::core::spsc_ring_conduit<char, 4096> rx_ring;
    tcb.rx_stream_ring = &rx_ring;

    // Simulate the Gateway and Defragmenter pushing parsed stream bytes into the RX buffer
    const char* http_req = "GET /fast HTTP/1.1\r\nHost: hft\r\n\r\n";
    size_t req_len = std::strlen(http_req);
    
    for(size_t i = 0; i < req_len; ++i) {
        *rx_ring.get_reserved_slot(i) = http_req[i];
    }
    rx_ring.commit_n(req_len);

    // The Application Layer reads from Virtual Socket identically to POSIX recv()
    virtual_tcp_socket sock(&tcb);
    alignas(64) char app_buffer[1024];
    ssize_t bytes_read = sock.recv(app_buffer, sizeof(app_buffer));
    
    EXPECT_EQ(bytes_read, req_len);

    // Application Layer immediately parses via the zero-allocation AVX Parser
    slabflux::transport::http_request_event ev;
    bool ok = slabflux::transport::http_avx_parser::parse(app_buffer, bytes_read, ev);
    
    ASSERT_TRUE(ok);
    EXPECT_EQ(ev.method, "GET");
    EXPECT_EQ(ev.uri, "/fast");
}

TEST(TcpFlowEngineTest, SequenceWraparoundSafety) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    
    // Set sequence right at the edge of 32-bit MAX
    tcb.rcv_nxt = 0xFFFFFFFA; // 4294967290
    tcb.rcv_wnd = 65535;

    // Receive a payload that crosses the 32-bit boundary
    raw_tcp_ipv4_frame data_frame{};
    data_frame.tcp_flags = FLAG_ACK | FLAG_PSH;
    data_frame.tcp_seq = slabflux::core::endian::host_to_network32(0xFFFFFFFA);
    
    uint32_t len = 20; // 0xFFFFFFFA + 20 = 0x0000000E (14)
    bool accepted = tcp_flow_engine::process_inbound(tcb, data_frame, len);
    
    ASSERT_TRUE(accepted);
    EXPECT_EQ(tcb.rcv_nxt, 14); // Wrapped around successfully!

    // Out-of-window rejection post-wrap
    raw_tcp_ipv4_frame out_of_wnd{};
    out_of_wnd.tcp_flags = FLAG_ACK | FLAG_PSH;
    out_of_wnd.tcp_seq = slabflux::core::endian::host_to_network32(0x0001000E); // Past 64k window
    
    uint32_t bad_len = 150;
    accepted = tcp_flow_engine::process_inbound(tcb, out_of_wnd, bad_len);
    
    // Out-of-window MUST elicit an ACK to resynchronize the peer.
    EXPECT_TRUE(accepted); 
    EXPECT_EQ(bad_len, 0); // Payload dropped natively
    EXPECT_TRUE(tcb.flags_pending & FLAG_ACK); // Resynchronization ACK queued
}

TEST(TcpFlowEngineTest, HalfClosedStateTransitions) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.rcv_nxt = 1000;
    tcb.rcv_wnd = 65535;

    virtual_tcp_socket sock(&tcb);
    
    // 1. Peer sends FIN (Active Close from Peer)
    raw_tcp_ipv4_frame fin_frame{};
    fin_frame.tcp_flags = FLAG_ACK | FLAG_FIN;
    fin_frame.tcp_seq = slabflux::core::endian::host_to_network32(1000);
    uint32_t len = 0;
    
    bool accepted = tcp_flow_engine::process_inbound(tcb, fin_frame, len);
    EXPECT_TRUE(accepted);
    
    // State must naturally progress to CLOSE_WAIT
    EXPECT_EQ(tcb.phase_mask, PHASE_CLOSE_WAIT);
    
    // 2. We should still be able to SEND data even though we can't receive!
    // This is a strict requirement for Half-Closed socket support.
    EXPECT_TRUE(sock.is_established()); 
    
    // 3. We call close() to finish the teardown
    sock.close();
    
    // Should transition from CLOSE_WAIT to LAST_ACK
    EXPECT_EQ(tcb.phase_mask, PHASE_LAST_ACK);
    EXPECT_TRUE(tcb.flags_pending & FLAG_FIN);
}

TEST(TcpFlowEngineTest, SelectiveAcknowledgmentParsing) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.sack_permitted = 1;
    tcb.snd_nxt = 5000;
    tcb.snd_una = 1000;
    tcb.rcv_nxt = 9000;
    tcb.rcv_wnd = 65535;

    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> unacked_ring;
    tcb.tx_unacked_ring = &unacked_ring;

    // Populate the unacked ring with 3 MSS segments
    for (int i = 0; i < 3; ++i) {
        auto* slot = unacked_ring.get_reserved_slot(0);
        slot->mbuf = mock_mbuf_alloc_flow(nullptr);
        auto* hdr = slot->get_header();
        hdr->ip_ihl_ver = 0x45;
        hdr->ip_len = slabflux::core::endian::host_to_network16(40 + 1460); // 20 IP + 20 TCP + 1460 Payload
        hdr->tcp_seq = slabflux::core::endian::host_to_network32(1000 + (i * 1460));
        hdr->tcp_data_offset = 0x50;
        hdr->tcp_flags = FLAG_ACK | FLAG_PSH;
        slot->set_payload_length(1460);
        unacked_ring.commit_n(1);
    }

    // Simulate an inbound Duplicate ACK containing a SACK Option Block
    // It explicitly acknowledges receiving the SECOND segment (Seq 2460 -> 3920)
    alignas(64) char raw_buffer[128] = {0};
    auto* sack_ack = reinterpret_cast<raw_tcp_ipv4_frame*>(raw_buffer);
    sack_ack->tcp_flags = FLAG_ACK;
    sack_ack->tcp_seq = slabflux::core::endian::host_to_network32(9000);
    sack_ack->tcp_ack = slabflux::core::endian::host_to_network32(1000);
    sack_ack->tcp_data_offset = 0x80; // 32 bytes (20 base + 12 options)

    uint8_t* opt = reinterpret_cast<uint8_t*>(raw_buffer) + 54;
    opt[0] = 0x01; opt[1] = 0x01; // NOP NOP
    opt[2] = 0x05; opt[3] = 0x0A; // SACK Kind (5), Length (10)
    *reinterpret_cast<uint32_t*>(&opt[4]) = slabflux::core::endian::host_to_network32(2460); // Left Edge
    *reinterpret_cast<uint32_t*>(&opt[8]) = slabflux::core::endian::host_to_network32(3920); // Right Edge

    uint32_t p_len = 0;
    tcp_flow_engine::process_inbound(tcb, *sack_ack, p_len);

    // Validate the segment in the ring was correctly tagged as SACKed natively
    auto* seg0 = unacked_ring.get_peek_slot(0); // 1000
    auto* seg1 = unacked_ring.get_peek_slot(1); // 2460
    auto* seg2 = unacked_ring.get_peek_slot(2); // 3920

    EXPECT_FALSE(seg0->is_sacked); // Lost segment
    EXPECT_TRUE(seg1->is_sacked);  // Explicitly SACKed by peer!
    EXPECT_FALSE(seg2->is_sacked); // Not acknowledged
}

TEST(TcpFlowEngineTest, RFC7323_SynWindowScaling) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_SYN_SENT;
    tcb.snd_wscale = 7; // We support scaling
    tcb.rcv_wnd = 65535;
    tcb.snd_nxt = 1;

    raw_tcp_ipv4_frame syn_ack{};
    syn_ack.tcp_flags = FLAG_SYN | FLAG_ACK;
    syn_ack.tcp_seq = slabflux::core::endian::host_to_network32(1000);
    syn_ack.tcp_ack = slabflux::core::endian::host_to_network32(1);
    syn_ack.tcp_window = slabflux::core::endian::host_to_network16(1000);
    
    uint32_t p_len = 0;
    tcp_flow_engine::process_inbound(tcb, syn_ack, p_len);
    
    // Window should be strictly 1000, NOT 128000!
    EXPECT_EQ(tcb.snd_wnd, 1000);
}

TEST(TcpFlowEngineTest, PAWS_Protection) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.rcv_nxt = 5000;
    tcb.rcv_wnd = 65535;
    tcb.ts_permitted = 1;
    tcb.ts_recent = 100;

    raw_tcp_ipv4_frame hdr{};
    hdr.tcp_flags = FLAG_ACK | FLAG_PSH;
    hdr.tcp_seq = slabflux::core::endian::host_to_network32(5000);
    hdr.tcp_data_offset = 0x80; // 32 bytes (20 + 12 opt)

    uint8_t* opt = reinterpret_cast<uint8_t*>(&hdr) + 54;
    opt[0] = 0x01; opt[1] = 0x01;
    opt[2] = 0x08; opt[3] = 0x0A;
    *reinterpret_cast<uint32_t*>(&opt[4]) = slabflux::core::endian::host_to_network32(50); // Older TS
    *reinterpret_cast<uint32_t*>(&opt[8]) = slabflux::core::endian::host_to_network32(0);

    uint32_t p_len = 100;
    bool accepted = tcp_flow_engine::process_inbound(tcb, hdr, p_len);
    
    EXPECT_TRUE(accepted); // Handled by PAWS (Challenge ACK)
    EXPECT_TRUE(tcb.flags_pending & FLAG_ACK); // Challenge ACK queued
}

TEST(TcpFlowEngineTest, ActiveSackAndTsGeneration) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.sack_permitted = 1;
    tcb.sack_left_edges[0] = 2000;
    tcb.sack_right_edges[0] = 3460;
    tcb.sack_valid[0] = true;
    tcb.ts_permitted = 1;

    alignas(64) char raw_buffer[128]{0};
    auto& hdr = *reinterpret_cast<raw_tcp_ipv4_frame*>(raw_buffer);
    tcp_flow_engine::prepare_outbound_header(tcb, hdr, 0, FLAG_ACK);

    EXPECT_EQ(hdr.tcp_data_offset, 0xB0); // 44 bytes (20 Base + 12 TS + 12 SACK)
    const uint8_t* opt = reinterpret_cast<const uint8_t*>(&hdr) + 54;
    EXPECT_EQ(opt[2], 0x08); // TS Kind generated!
    EXPECT_EQ(opt[14], 0x05); // SACK Kind generated!
}

// ============================================================================
// L7 / L4 BRIDGE INTEGRATION TESTS (VIRTUAL CONDUIT & ACCEPTOR)
// ============================================================================

struct MockAcceptHandler {
    bool called = false;
    uint32_t accepted_conn_id = 0;
    void on_accept(virtual_tcp_socket& sock, uint32_t conn_id) {
        called = true;
        accepted_conn_id = conn_id;
        (void)sock; // Avoid unused warning
    }
};

struct MockGatewayForAcceptor {
    tcp_transmission_control_block tcb{};
    tcp_transmission_control_block& get_tcb(size_t) { return tcb; }
};

TEST(VirtualTcpAcceptorTest, PollAcceptDrainsGatewayQueue) {
    MockGatewayForAcceptor gateway;
    slabflux::core::spsc_ring_conduit<uint32_t, 1024> accept_queue;
    virtual_tcp_acceptor<MockGatewayForAcceptor> acceptor(gateway, &accept_queue);
    
    uint32_t* slot = accept_queue.get_reserved_slot(0);
    *slot = 42;
    accept_queue.commit_n(1);
    
    MockAcceptHandler handler;
    acceptor.poll_accept(handler);
    
    EXPECT_TRUE(handler.called);
    EXPECT_EQ(handler.accepted_conn_id, 42);
    EXPECT_EQ(accept_queue.available_to_peek(), 0);
}

struct DummyL7Event {
    static constexpr uint32_t TYPE_ID = 1;
    uint64_t payload;
};

struct MockEventSink {
    std::vector<DummyL7Event> received;
    void on_event(const DummyL7Event& ev) {
        received.push_back(ev);
    }
};

TEST(NetworkConduitVirtualTest, ActiveOpenConnectionLifecycle) {
    tcp_transmission_control_block tcb{};
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    tcb.tx_egress_conduit = &tx_ring;
    tcb.tx_mbuf_pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    tcb.target_mac[0] = 0xAA; // Resolve ARP natively
    virtual_tcp_socket sock(&tcb);
    network_conduit_virtual<DummyL7Event> conduit;
    conduit.bind(&sock);
    
    // Initiate Active Open to an arbitrary IP
    conduit.open(0x08080808, 80);
    
    EXPECT_EQ(tcb.phase_mask, PHASE_SYN_SENT);
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* syn_pkt = tx_ring.get_peek_slot(0);
    EXPECT_EQ(syn_pkt->get_header()->tcp_flags, FLAG_SYN);
    EXPECT_EQ(tcb.remote_ipv4, 0x08080808);
    EXPECT_EQ(tcb.remote_port, 80);
    EXPECT_FALSE(conduit.is_dead());
}

TEST(NetworkConduitVirtualTest, PushAndPollTxSerialization) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.snd_wnd = 65535;
    tcb.snd_nxt = 1000;
    tcb.snd_una = 1000;
    tcb.cwnd = 14600;
    tcb.tx_mbuf_pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);

    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> unacked_ring;
    tcb.tx_egress_conduit = &tx_ring;
    tcb.tx_unacked_ring = &unacked_ring;

    virtual_tcp_socket sock(&tcb);
    network_conduit_virtual<DummyL7Event> conduit;
    conduit.bind(&sock);
    
    DummyL7Event ev1{0xCAFEBABE};
    DummyL7Event ev2{0xDEADBEEF};
    
    EXPECT_TRUE(conduit.push(ev1));
    EXPECT_TRUE(conduit.push(ev2));
    
    conduit.poll_tx(); // Execute L7 to L4 serialization and push to physical wire
    
    size_t expected_size = sizeof(slabflux::net::wire_frame<DummyL7Event>);
    EXPECT_EQ(tcb.snd_nxt, 1000 + (expected_size * 2));
    EXPECT_EQ(tx_ring.available_to_peek(), 2); // 2 fragments generated natively
    
    auto* seg1 = tx_ring.get_peek_slot(0);
    EXPECT_EQ(seg1->get_payload_length(), expected_size);
    auto* wire_events1 = reinterpret_cast<slabflux::net::wire_frame<DummyL7Event>*>(seg1->get_payload());
    DummyL7Event out1;
    slabflux::net::wire_protocol<DummyL7Event>::deserialize(wire_events1[0], out1);
    EXPECT_EQ(out1.payload, 0xCAFEBABE);
    
    auto* seg2 = tx_ring.get_peek_slot(1);
    EXPECT_EQ(seg2->get_payload_length(), expected_size);
    auto* wire_events2 = reinterpret_cast<slabflux::net::wire_frame<DummyL7Event>*>(seg2->get_payload());
    DummyL7Event out2;
    slabflux::net::wire_protocol<DummyL7Event>::deserialize(wire_events2[0], out2);
    EXPECT_EQ(out2.payload, 0xDEADBEEF);
}

TEST(NetworkConduitVirtualTest, PollRxPartialDeserialization) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    
    slabflux::core::spsc_ring_conduit<char, 4096> rx_ring;
    tcb.rx_stream_ring = &rx_ring;

    virtual_tcp_socket sock(&tcb);
    network_conduit_virtual<DummyL7Event> conduit;
    conduit.bind(&sock);
    
    size_t expected_size = sizeof(slabflux::net::wire_frame<DummyL7Event>);
    DummyL7Event ev1{0x1122334455667788};
    DummyL7Event ev2{0x99AABBCCDDEEFF00};
    
    slabflux::net::wire_frame<DummyL7Event> frame1, frame2;
    slabflux::net::wire_protocol<DummyL7Event>::serialize(frame1, ev1);
    slabflux::net::wire_protocol<DummyL7Event>::serialize(frame2, ev2);
    
    char* raw1 = reinterpret_cast<char*>(&frame1);
    char* raw2 = reinterpret_cast<char*>(&frame2);
    
    size_t partial_size = expected_size / 2;
    for (size_t i = 0; i < expected_size; ++i) *rx_ring.get_reserved_slot(i) = raw1[i];
    for (size_t i = 0; i < partial_size; ++i) *rx_ring.get_reserved_slot(expected_size + i) = raw2[i]; // Partial
    
    rx_ring.commit_n(expected_size + partial_size);
    
    MockEventSink sink;
    conduit.poll_rx(sink);
    
    // Only 1 full event should be fully materialized. The partial event must remain buffered.
    EXPECT_EQ(sink.received.size(), 1);
    EXPECT_EQ(sink.received[0].payload, 0x1122334455667788);
    
    // Arrive later and complete the second event
    for (size_t i = partial_size; i < expected_size; ++i) *rx_ring.get_reserved_slot(i - partial_size) = raw2[i];
    rx_ring.commit_n(expected_size - partial_size);
    
    conduit.poll_rx(sink);
    
    EXPECT_EQ(sink.received.size(), 2);
    EXPECT_EQ(sink.received[1].payload, 0x99AABBCCDDEEFF00);
}

TEST(NetworkConduitVirtualTest, BackpressureYieldingAndResumption) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.snd_wnd = 10; // Artificially tiny window to force backpressure yielding
    tcb.snd_nxt = 1000;
    tcb.snd_una = 1000;
    tcb.cwnd = 14600;
    tcb.tx_mbuf_pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);

    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> unacked_ring;
    tcb.tx_egress_conduit = &tx_ring;
    tcb.tx_unacked_ring = &unacked_ring;

    virtual_tcp_socket sock(&tcb);
    network_conduit_virtual<DummyL7Event> conduit;
    conduit.bind(&sock);
    
    DummyL7Event ev1{0x1122334455667788};
    EXPECT_TRUE(conduit.push(ev1));
    
    // First poll will hit the 10-byte TCP window limit, send partial data, and cleanly yield
    conduit.poll_tx();
    EXPECT_EQ(tcb.snd_nxt, 1010); 
    EXPECT_EQ(tx_ring.available_to_peek(), 1);
    
    // Window opens up (simulate receiving a Window Update ACK from the peer)
    tcb.snd_wnd = 65535;
    
    // Second poll will seamlessly resume serialization from the internal byte offset
    conduit.poll_tx();
    
    size_t expected_size = sizeof(slabflux::net::wire_frame<DummyL7Event>);
    EXPECT_EQ(tcb.snd_nxt, 1000 + expected_size);
}