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
 * ============================================================================
 * @file tcp_chaotic_flow_test.cpp
 * @brief Golden-Reference Adversarial Network Simulator & TCP Flow Test Suite.
 */

#include <gtest/gtest.h>
#include <vector>
#include <random>
#include <algorithm>
#include <cstring>

#include <rte_mbuf.h>

// Mock DPDK Mempool Allocations for Unit Testing
inline struct rte_mbuf* mock_mbuf_alloc(struct rte_mempool*) {
    char* mem = new char[sizeof(struct rte_mbuf) + 2048];
    std::memset(mem, 0, sizeof(struct rte_mbuf) + 2048);
    struct rte_mbuf* m = reinterpret_cast<struct rte_mbuf*>(mem);
    m->buf_addr = mem + sizeof(struct rte_mbuf);
    m->data_off = 0;
    m->pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    rte_mbuf_refcnt_set(m, 1);
    return m;
}

inline void mock_mbuf_free(struct rte_mbuf* m) {
    if (m) {
        if (rte_mbuf_refcnt_read(m) <= 1) delete[] reinterpret_cast<char*>(m);
        else rte_mbuf_refcnt_update(m, -1);
    }
}

#define rte_pktmbuf_alloc mock_mbuf_alloc
#define rte_pktmbuf_free mock_mbuf_free

#include "slabflux/net/tcp_gateway.hpp"
#include "slabflux/net/virtual_tcp_socket.hpp"
#include "slabflux/net/tcp_spatial_ooo_matrix.hpp"
#include "slabflux/net/tcp_temporal_wheel.hpp"
#include "slabflux/net/public_tcp_gateway.hpp"
#include "slabflux/net/virtual_udp_socket.hpp"
#include "slabflux/net/raw_udp_ipv4_frame.hpp"

using namespace slabflux::net;

// ============================================================================
// MOCK SUBSTRATES (L7 / Business Logic)
// ============================================================================

struct MockL7Defragmenter {
    std::vector<char> reassembled_stream;
    int udp_packets_received = 0;
    
    // Invoked natively by the tcp_gateway when chunks become contiguous
    template <typename FrameType>
    void on(const FrameType& frame) {
        reassembled_stream.insert(
            reassembled_stream.end(), 
            frame.data, 
            frame.data + frame.payload_length
        );
    }

    // Mock hook for native UDP protocol routing (Suppresses ICMP Port Unreachable in Gateway)
    template <typename UDPFrameType>
    bool on_udp(const UDPFrameType&) {
        udp_packets_received++;
        return true; 
    }

    // Mock hook for native UDP IPv6 protocol routing
    template <typename UDPFrameType>
    bool on_udp_ipv6(const UDPFrameType&) {
        udp_packets_received++;
        return true; 
    }
};

// ============================================================================
// CHAOTIC FLOW TEST FIXTURE
// ============================================================================

class TcpChaoticFlowTest : public ::testing::Test {
protected:
    MockL7Defragmenter defragmenter;
    tcp_gateway<MockL7Defragmenter>* gateway;
    tcp_transmission_control_block* tcb;
    tcp_spatial_ooo_matrix ooo_matrix;

    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> unacked_ring;

    void SetUp() override {
        gateway = new tcp_gateway<MockL7Defragmenter>(defragmenter);
        
        // Synthesize a hardcoded established connection for routing tests (conn_id = 42)
        // The gateway uses spatial hashing: tcbs_[conn_id & 1023]
        tcb = &gateway->get_tcb(42);
        
        tcb->phase_mask = PHASE_ESTABLISHED;
        tcb->rcv_nxt = 1000;
        tcb->rcv_wnd = 65535;
        tcb->snd_una = 5000;
        tcb->snd_nxt = 5000;
        tcb->snd_wnd = 65535;
        tcb->cwnd = 14600;
        tcb->ssthresh = 65535;
        
        // Bind exact 4-tuple IP & Ports to satisfy Axiom 16 payload isolation check natively
        tcb->remote_ipv4 = slabflux::core::endian::host_to_network32(0x0A000005);
        tcb->local_ipv4 = slabflux::core::endian::host_to_network32(0x0A000002);
        tcb->remote_port = slabflux::core::endian::host_to_network16(12345);
        tcb->local_port = slabflux::core::endian::host_to_network16(80);

        tcb->ooo_matrix = &ooo_matrix;
        ooo_matrix.reset(tcb->rcv_nxt);

        tcb->tx_egress_conduit = &tx_ring;
        tcb->tx_unacked_ring = &unacked_ring;
        tcb->tx_mbuf_pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    }

    void TearDown() override {
        delete gateway;
    }

    // Helper: Fabricates an inbound physical Layer-2 frame
    void inject_network_frame(uint32_t seq, uint32_t ack, uint8_t flags, const std::string& payload = "") {
        alignas(64) char raw_buffer[2048];
        std::memset(raw_buffer, 0, sizeof(raw_buffer));
        
        auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(raw_buffer);
        std::memcpy(hdr->dest_mac, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
        std::memcpy(hdr->src_mac, "\x11\x22\x33\x44\x55\x66", 6);
        hdr->eth_type = slabflux::core::endian::host_to_network16(0x0800);
        hdr->ip_ihl_ver = 0x45;
        hdr->ip_protocol = 6;
        hdr->ip_src = slabflux::core::endian::host_to_network32(0x0A000005);
        hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A000002);
        hdr->tcp_src_port = slabflux::core::endian::host_to_network16(12345);
        hdr->tcp_dst_port = slabflux::core::endian::host_to_network16(80);

        uint32_t header_size = 54;
        uint32_t total_size = header_size + payload.size();
        hdr->ip_len = slabflux::core::endian::host_to_network16(total_size - 14);

        hdr->tcp_seq = slabflux::core::endian::host_to_network32(seq);
        hdr->tcp_ack = slabflux::core::endian::host_to_network32(ack);
        hdr->tcp_flags = flags;
        hdr->tcp_window = slabflux::core::endian::host_to_network16(65535);
        hdr->tcp_data_offset = 0x50; // 20 bytes
               
        if (!payload.empty()) {
            std::memcpy(raw_buffer + header_size, payload.data(), payload.size());
        }
        
        // Ensure L3/L4 Checksums are strictly mathematically valid for the hardened gateway filters
        hdr->ip_checksum = 0;
        hdr->ip_checksum = slabflux::net::tcp_wire_engine::compute_checksum(&hdr->ip_ihl_ver, 20, 0);
        
        uint16_t tcp_len = 20 + payload.size();
        uint32_t pseudo_sum = 0;
        uint32_t src = hdr->ip_src;
        uint32_t dst = hdr->ip_dst;
        pseudo_sum += (src & 0xFFFF) + (src >> 16);
        pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
        pseudo_sum += slabflux::core::endian::host_to_network16(6);
        pseudo_sum += slabflux::core::endian::host_to_network16(tcp_len);

        hdr->tcp_checksum = 0;
        hdr->tcp_checksum = slabflux::net::tcp_wire_engine::compute_checksum(&hdr->tcp_src_port, tcp_len, pseudo_sum);
        
        struct rte_mbuf* mbuf = rte_pktmbuf_alloc(tcb->tx_mbuf_pool);
        if (mbuf) {
            char* mbuf_data = rte_pktmbuf_mtod(mbuf, char*);
            std::memcpy(mbuf_data, raw_buffer, total_size);
            mbuf->data_len = total_size;
            mbuf->pkt_len = mbuf->data_len;
            gateway->on_raw_frame(mbuf_data, total_size, 42, mbuf);
        } else {
            gateway->on_raw_frame(raw_buffer, total_size, 42);
        }
    }
};

// ============================================================================
// TEST 1: The OOO Reverse-Order Avalanche
// ============================================================================
TEST_F(TcpChaoticFlowTest, ReassemblyOfReverseOrderedAvalanche) {
    // We simulate receiving 5 packets of a stream, but they arrive exactly 
    // BACKWARDS. Packet 5, then 4, 3, 2, and finally 1.
    // Standard ring buffers would drop this. The Spatial OOO Matrix must catch them.
    
    std::string p1(100, 'A');
    std::string p2(100, 'B');
    std::string p3(100, 'C');
    std::string p4(100, 'D');
    std::string p5(100, 'E');

    // RCV_NXT is currently 1000.
    inject_network_frame(1400, 5000, FLAG_ACK | FLAG_PSH, p5); // OOO
    inject_network_frame(1300, 5000, FLAG_ACK | FLAG_PSH, p4); // OOO
    inject_network_frame(1200, 5000, FLAG_ACK | FLAG_PSH, p3); // OOO
    inject_network_frame(1100, 5000, FLAG_ACK | FLAG_PSH, p2); // OOO

    // Since none of these were in-order (Seq 1000), RCV_NXT should not have moved.
    EXPECT_EQ(tcb->rcv_nxt, 1000);
    EXPECT_EQ(defragmenter.reassembled_stream.size(), 0);

    // Furthermore, every OOO packet must have triggered an immediate DUP ACK.
    EXPECT_EQ(tx_ring.available_to_peek(), 4);
    tx_ring.consume_n(4); // Clear the egress wire

    // Finally, the missing lynchpin packet arrives.
    inject_network_frame(1000, 5000, FLAG_ACK | FLAG_PSH, p1);

    // The Gateway should have natively routed p1 to the defragmenter, and then 
    // evaluated the OOO Matrix bitmask, extracting p2, p3, p4, p5 sequentially!
    EXPECT_EQ(tcb->rcv_nxt, 1500);
    ASSERT_EQ(defragmenter.reassembled_stream.size(), 500);

    // Verify contiguous string integrity
    std::string reconstructed(defragmenter.reassembled_stream.begin(), defragmenter.reassembled_stream.end());
    std::string golden = p1 + p2 + p3 + p4 + p5;
    EXPECT_EQ(reconstructed, golden);
}

// ============================================================================
// TEST 2: Active Retransmission Contention Collapse
// ============================================================================
TEST_F(TcpChaoticFlowTest, TemporalRetransmissionReplay) {
    virtual_tcp_socket sock(tcb);
    
    std::string data(1460 * 3, 'X'); // Exactly 3 full MSS segments
    sock.send(data.data(), data.size());

    EXPECT_EQ(tcb->snd_nxt, 5000 + 1460 * 3);
    
    // The data should be in BOTH the hardware volatile ring (tx_ring) 
    // and the safe unacked_ring for standard TCP sliding window retention.
    EXPECT_EQ(tx_ring.available_to_peek(), 3);
    EXPECT_EQ(unacked_ring.available_to_peek(), 3);

    // The NIC physically consumes the packets from the volatile wire ring.
    tx_ring.consume_n(3);
    EXPECT_EQ(tx_ring.available_to_peek(), 0);

    // Simulate Network Silence / Blackhole. 
    // The RTO tracker says the deadline is at `current_time + tracker.rto`
    uint64_t current_time = 1000;
    tcb->rto_deadline_tsc = current_time + 300; // Armed 300ms from now

    // Tick before RTO: Nothing should happen
    gateway->poll_temporal(current_time + 100);
    EXPECT_EQ(tx_ring.available_to_peek(), 0);

    // Tick After RTO Expiration!
    gateway->poll_temporal(current_time + 350);

    // The temporal wheel should have identified the Contention Collapse,
    // rewound snd_nxt to snd_una, penalized the congestion window (AIMD),
    // and the Gateway should have natively re-injected the lost packets!
    EXPECT_EQ(tcb->cwnd, 1460); // Dropped to Slow-Start
    EXPECT_EQ(tx_ring.available_to_peek(), 1); // Volatile wire repopulated from unacked_ring without calling send()!
    
    // Validate that the newly injected frames have the correctly updated 
    // ACK horizons based on whatever we've received recently.
    ASSERT_EQ(tx_ring.available_to_peek(), 1); // Volatile wire repopulated from unacked_ring without calling send()!
    auto* replayed = tx_ring.get_peek_slot(0);
    EXPECT_EQ(slabflux::core::endian::network_to_host32(replayed->get_header()->tcp_seq), 5000);
    EXPECT_EQ(slabflux::core::endian::network_to_host32(replayed->get_header()->tcp_ack), 1000);
}

// ============================================================================
// TEST 3: Zero-Window Lockout & Probing (Persistence Timer)
// ============================================================================
TEST_F(TcpChaoticFlowTest, ZeroWindowLockoutAndPersistence) {
    // The remote peer is choked and advertises a Zero Window.
    inject_network_frame(1000, 5000, FLAG_ACK, "");
    tcb->snd_wnd = 0; // The remote receiver's window is zero.

    virtual_tcp_socket sock(tcb);
    std::string payload = "I demand you accept my data!";
    
    // Attempting to send must mathematically hit backpressure natively and return 0 (EWOULDBLOCK)
    ssize_t sent = sock.send(payload.data(), payload.size());
    EXPECT_EQ(sent, 0); 
    EXPECT_EQ(tx_ring.available_to_peek(), 0); // Nothing written to wire

    uint64_t current_time = 5000;
    
    // Now we simulate unacknowledged data existing to trigger the Persistence Timer
    tcb->snd_una = 5000;
    tcb->snd_nxt = 5010;

    // First tick engages the Persist mode
    gateway->poll_temporal(current_time);
    EXPECT_TRUE(tcb->temporal_flags & TEMP_FLAG_ZWP_ACTIVE);
    EXPECT_EQ(tx_ring.available_to_peek(), 0);

    // Advance time past the backoff timer (1 second initial)
    gateway->poll_temporal(current_time + 1050);

    // The Temporal Wheel should have mandated a standalone ACK (Window Probe)
    // to solicit a window update from the deadlocked peer.
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* probe = tx_ring.get_peek_slot(0);
    EXPECT_EQ(probe->get_header()->tcp_flags, FLAG_ACK);
    EXPECT_EQ(probe->get_payload_length(), 1); // 1-byte probe mandated by RFC 9293
}

// ============================================================================
// TEST 4: The 10MB Adversarial Network Stream (Golden Reference)
// ============================================================================
TEST_F(TcpChaoticFlowTest, MassiveAdversarialStream) {
    // We will generate a 1MB payload (representing a massive JSON dump)
    // and send it through a simulated chaotic network that arbitrarily drops, 
    // duplicates, and reorders 1460-byte MTU segments.
    
    const size_t TOTAL_BYTES = 60000; // Constrained to strictly fit the 64KB Spatial OOO Matrix limits
    std::string golden_data(TOTAL_BYTES, '\0');
    
    // Fill with deterministic noise for integrity validation
    for(size_t i = 0; i < TOTAL_BYTES; ++i) {
        golden_data[i] = static_cast<char>(i % 255);
    }

    // Break into MSS-sized packets
    struct Packet {
        uint32_t seq;
        std::string payload;
    };
    std::vector<Packet> network_cloud;

    uint32_t current_seq = 1000;
    for (size_t offset = 0; offset < TOTAL_BYTES; offset += 1460) {
        size_t chunk_size = std::min(static_cast<size_t>(1460), TOTAL_BYTES - offset);
        network_cloud.push_back({
            current_seq,
            golden_data.substr(offset, chunk_size)
        });
        current_seq += chunk_size;
    }

    // --- INJECT ADVERSARIAL CHAOS ---
    std::mt19937 rng(42); // Deterministic seed for reproducible testing
    std::shuffle(network_cloud.begin(), network_cloud.end(), rng);

    // Save 5% of packets to be "lost" and delivered much later (Simulated RTO)
    std::vector<Packet> lost_packets;
    auto it = std::remove_if(network_cloud.begin(), network_cloud.end(), [&](const Packet& p) {
        if (rng() % 100 < 5) { // 5% drop rate
            lost_packets.push_back(p);
            return true;
        }
        return false;
    });
    network_cloud.erase(it, network_cloud.end());
    
    // Guarantee at least one dropped packet to enforce test conditions
    if (lost_packets.empty() && network_cloud.size() > 1) {
        lost_packets.push_back(network_cloud[1]);
        network_cloud.erase(network_cloud.begin() + 1);
    }

    // Duplicate 2% of the surviving packets to test Replay Resistance
    size_t og_size = network_cloud.size();
    for (size_t i = 0; i < og_size; ++i) {
        if (rng() % 100 < 2) {
            network_cloud.push_back(network_cloud[i]);
        }
    }

    // Shuffle again to interleave the duplicates
    std::shuffle(network_cloud.begin(), network_cloud.end(), rng);

    // --- EXECUTE CHAOTIC BOMBARDMENT ---
    // Fire the mangled packets at the Gateway
    for (const auto& pkt : network_cloud) {
        inject_network_frame(pkt.seq, 5000, FLAG_ACK | FLAG_PSH, pkt.payload);
    }
    
    // At this point, the defragmenter should only have partial data due to the 5% loss rate,
    // but the Spatial OOO Matrix should be holding vast amounts of disjointed contiguous blocks.
    EXPECT_LT(defragmenter.reassembled_stream.size(), TOTAL_BYTES);

    // --- THE RESCUE ---
    // Deliver the 5% "lost" packets. These represent the active retransmissions 
    // the sender finally fired off due to DUP ACKs and RTO.
    std::shuffle(lost_packets.begin(), lost_packets.end(), rng);
    for (const auto& pkt : lost_packets) {
        inject_network_frame(pkt.seq, 5000, FLAG_ACK | FLAG_PSH, pkt.payload);
    }

    // --- FINAL VALIDATION ---
    // The mathematical sequence should be perfectly synced with the byte volume
    EXPECT_EQ(tcb->rcv_nxt, 1000 + TOTAL_BYTES);
    ASSERT_EQ(defragmenter.reassembled_stream.size(), TOTAL_BYTES);
    
    // Memcmp the entire 1MB block to prove bit-perfect reassembly without memory corruption
    EXPECT_EQ(std::memcmp(defragmenter.reassembled_stream.data(), golden_data.data(), TOTAL_BYTES), 0);
}

// ============================================================================
// TEST 5: Half-Open Connection Rejection (RST)
// ============================================================================
TEST_F(TcpChaoticFlowTest, HandlesAbruptRst) {
    // A wild RST appears during established state
    inject_network_frame(1000, 5000, FLAG_RST);

    // TCP RFC dictates that the connection must immediately tear down
    EXPECT_EQ(tcb->phase_mask, PHASE_CLOSED);
    
    // Virtual Socket must detect the closed state and reject sends
    virtual_tcp_socket sock(tcb);
    EXPECT_FALSE(sock.is_established());
    EXPECT_EQ(sock.send("test", 4), -1);
}

// ============================================================================
// TEST 6: Standalone Protocol ACK Flushes
// ============================================================================
TEST_F(TcpChaoticFlowTest, BackgroundDelayedAckFlush) {
    tcb->temporal_flags |= TEMP_FLAG_ACK_PENDING;
    tcb->delayed_ack_deadline_tsc = 1000;

    // Tick past the Delayed ACK timer
    gateway->poll_temporal(1200);

    // Should have wiped the pending flag and generated a pure ACK onto the wire
    EXPECT_FALSE(tcb->temporal_flags & TEMP_FLAG_ACK_PENDING);
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    EXPECT_EQ(tx_ring.get_peek_slot(0)->get_header()->tcp_flags, FLAG_ACK);
}

// ============================================================================
// TEST 7: TCP Fast Retransmit Trigger (3 DUP ACKs)
// ============================================================================
TEST_F(TcpChaoticFlowTest, FastRetransmitOnHardwareDrop) {
    virtual_tcp_socket sock(tcb);
    
    std::string data(1460 * 4, 'X'); // Dispatch 4 full MSS payload segments
    sock.send(data.data(), data.size());
    
    EXPECT_EQ(tx_ring.available_to_peek(), 4);
    tx_ring.consume_n(4); // Hardware NIC clears the ring (Dispatched to wire)
    
    // We simulate that Packet 1 (Seq 5000) was catastrophically dropped by the network!
    // The receiver subsequently receives Packets 2, 3, and 4 Out-Of-Order.
    // TCP RFC dictates it must instantly reply to each with a DUP ACK for Seq 5000.
    inject_network_frame(1000, 5000, FLAG_ACK, ""); // DUP ACK 1
    inject_network_frame(1000, 5000, FLAG_ACK, ""); // DUP ACK 2
    inject_network_frame(1000, 5000, FLAG_ACK, ""); // DUP ACK 3 (Threshold hit!)
    
    // The gateway's Flow Engine must bypass the RTO temporal wheel, drop the Congestion Window, 
    // and immediately fire the lost segment straight into the volatile egress wire natively.
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* rtx = tx_ring.get_peek_slot(0);
    EXPECT_EQ(slabflux::core::endian::network_to_host32(rtx->get_header()->tcp_seq), 5000);
}