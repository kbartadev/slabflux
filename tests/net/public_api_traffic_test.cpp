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
 * @file public_api_traffic_test.cpp
 * @brief Comprehensive functional testing for public_tcp_gateway handling standard TCP/UDP traffic.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include <string>

#include <rte_mbuf.h>

// Mock DPDK Mempool Allocations for Unit Testing
inline struct rte_mbuf* mock_mbuf_alloc_traf(struct rte_mempool*) {
    char* mem = new char[sizeof(struct rte_mbuf) + 2048];
    std::memset(mem, 0, sizeof(struct rte_mbuf) + 2048);
    struct rte_mbuf* m = reinterpret_cast<struct rte_mbuf*>(mem);
    m->buf_addr = mem + sizeof(struct rte_mbuf);
    m->data_off = 0;
    m->pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    rte_mbuf_refcnt_set(m, 1);
    return m;
}

inline void mock_mbuf_free_traf(struct rte_mbuf* m) {
    if (m) {
        if (rte_mbuf_refcnt_read(m) <= 1) delete[] reinterpret_cast<char*>(m);
        else rte_mbuf_refcnt_update(m, -1);
    }
}

#undef rte_pktmbuf_alloc
#undef rte_pktmbuf_free
#define rte_pktmbuf_alloc mock_mbuf_alloc_traf
#define rte_pktmbuf_free mock_mbuf_free_traf

#include "slabflux/net/public_tcp_gateway.hpp"
#include "slabflux/net/virtual_tcp_socket.hpp"
#include "slabflux/net/virtual_udp_socket.hpp"
#include "slabflux/net/raw_tcp_ipv6_frame.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"

using namespace slabflux::net;

struct TrafficMockApp {
    std::vector<char> tcp_data;
    std::vector<char> udp_data;
    uint32_t active_conn_id = 0;

    template <typename FrameType>
    void on(const FrameType& frame) {
        active_conn_id = frame.connection_id;
        tcp_data.insert(tcp_data.end(), frame.data, frame.data + frame.payload_length);
    }

    template <typename FrameType>
    bool on_udp(const FrameType& frame) {
        udp_data.insert(udp_data.end(), frame.data, frame.data + frame.payload_length);
        return true;
    }

    template <typename FrameType>
    bool on_udp_ipv6(const FrameType& frame) {
        udp_data.insert(udp_data.end(), frame.data, frame.data + frame.payload_length);
        return true;
    }
};

class PublicApiTrafficTest : public ::testing::Test {
protected:
    TrafficMockApp app;
    public_tcp_gateway<TrafficMockApp>* gateway;
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    slabflux::core::spsc_ring_conduit<uint32_t, 1024> accept_ring;
    struct rte_mempool* dummy_pool;

    void SetUp() override {
        gateway = new public_tcp_gateway<TrafficMockApp>(app);
        dummy_pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
        gateway->bind_conduits(&tx_ring, &accept_ring);
        gateway->bind_mempool(dummy_pool);
        gateway->set_local_identity(
            slabflux::core::endian::host_to_network32(0x0A000001), 
            reinterpret_cast<const uint8_t*>("\x00\x11\x22\x33\x44\x55"),
            slabflux::core::endian::host_to_network32(0xFFFFFF00)
        );
        
        uint64_t local_ipv6[2] = { slabflux::core::endian::host_to_network64(0xfe80000000000000ULL), slabflux::core::endian::host_to_network64(0x0000000000000001ULL) };
        gateway->set_local_identity_v6(local_ipv6);
        
        tcp_syn_cookie::seed_keys();
        for (auto& entry : tcp_syn_cookie::replay_cache) {
            entry.store(0, std::memory_order_relaxed);
        }
    }

    void TearDown() override {
        delete gateway;
    }

    void init_valid_tcp_base(raw_tcp_ipv4_frame* hdr) {
        std::memset(hdr, 0, sizeof(raw_tcp_ipv4_frame));
        hdr->eth_type = slabflux::core::endian::host_to_network16(0x0800);
        hdr->ip_ihl_ver = 0x45;
        hdr->ip_len = slabflux::core::endian::host_to_network16(40);
        hdr->ip_ttl = 64;
        hdr->ip_protocol = 6;
        hdr->ip_src = slabflux::core::endian::host_to_network32(0x08080808); 
        hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A000001); 
        hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
        hdr->tcp_src_port = slabflux::core::endian::host_to_network16(12345);
        hdr->tcp_dst_port = slabflux::core::endian::host_to_network16(80);
        hdr->tcp_data_offset = 0x50; 
        hdr->tcp_flags = FLAG_SYN;
        std::memcpy(hdr->dest_mac, "\x00\x11\x22\x33\x44\x55", 6); 
        std::memcpy(hdr->src_mac, "\x12\x22\x33\x44\x55\x66", 6); 
    }

    void init_valid_tcp_ipv6_base(raw_tcp_ipv6_frame* hdr) {
        std::memset(hdr, 0, sizeof(raw_tcp_ipv6_frame));
        hdr->eth_type = slabflux::core::endian::host_to_network16(0x86DD);
        hdr->ipv6_flow = slabflux::core::endian::host_to_network32(0x60000000);
        hdr->ipv6_plen = slabflux::core::endian::host_to_network16(20);
        hdr->ipv6_nxt = 6;
        hdr->ipv6_hlim = 64;
        hdr->ipv6_src[0] = slabflux::core::endian::host_to_network64(0x20010db800000000ULL); 
        hdr->ipv6_src[1] = slabflux::core::endian::host_to_network64(0x0000000000000002ULL); 
        hdr->ipv6_dst[0] = slabflux::core::endian::host_to_network64(0xfe80000000000000ULL); 
        hdr->ipv6_dst[1] = slabflux::core::endian::host_to_network64(0x0000000000000001ULL);
        hdr->tcp_src_port = slabflux::core::endian::host_to_network16(12345);
        hdr->tcp_dst_port = slabflux::core::endian::host_to_network16(80);
        hdr->tcp_data_offset = 0x50; 
        hdr->tcp_flags = FLAG_SYN;
        std::memcpy(hdr->dest_mac, "\x00\x11\x22\x33\x44\x55", 6); 
        std::memcpy(hdr->src_mac, "\x12\x22\x33\x44\x55\x66", 6); 
    }

    void inject_tcp_frame(uint32_t seq, uint32_t ack, uint8_t flags, const std::string& payload = "", uint16_t src_port = 12345, uint16_t dst_port = 80) {
        alignas(64) char raw_buffer[2048];
        auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(raw_buffer);
        init_valid_tcp_base(hdr);
        hdr->tcp_src_port = slabflux::core::endian::host_to_network16(src_port);
        hdr->tcp_dst_port = slabflux::core::endian::host_to_network16(dst_port);
        
        uint32_t header_size = 54;
        uint32_t total_size = header_size + payload.size();
        hdr->ip_len = slabflux::core::endian::host_to_network16(total_size - 14);

        hdr->tcp_seq = slabflux::core::endian::host_to_network32(seq);
        hdr->tcp_ack = slabflux::core::endian::host_to_network32(ack);
        hdr->tcp_flags = flags;
        hdr->tcp_window = slabflux::core::endian::host_to_network16(65535);
        
        if (!payload.empty()) {
            std::memcpy(raw_buffer + header_size, payload.data(), payload.size());
        }
        
        hdr->ip_checksum = 0;
        hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
        
        uint16_t tcp_len = 20 + payload.size();
        uint32_t pseudo_sum = 0;
        uint32_t src = hdr->ip_src;
        uint32_t dst = hdr->ip_dst;
        pseudo_sum += (src & 0xFFFF) + (src >> 16);
        pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
        pseudo_sum += slabflux::core::endian::host_to_network16(6);
        pseudo_sum += slabflux::core::endian::host_to_network16(tcp_len);

        hdr->tcp_checksum = 0;
        hdr->tcp_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 34, tcp_len, pseudo_sum);
        
        struct rte_mbuf* mbuf = rte_pktmbuf_alloc(dummy_pool);
        if (mbuf) {
            char* mbuf_data = rte_pktmbuf_mtod(mbuf, char*);
            std::memset(mbuf_data, 0, total_size);
            std::memcpy(mbuf_data, raw_buffer, total_size);
            mbuf->data_len = total_size;
            mbuf->pkt_len = total_size;
            gateway->on_raw_frame(mbuf_data, total_size, 1000, mbuf);
        } else {
            gateway->on_raw_frame(raw_buffer, total_size, 1000);
        }
    }

    void inject_tcp_ipv6_frame(uint32_t seq, uint32_t ack, uint8_t flags, const std::string& payload = "", uint16_t src_port = 12345, uint16_t dst_port = 80) {
        alignas(64) char raw_buffer[2048];
        auto* hdr = reinterpret_cast<raw_tcp_ipv6_frame*>(raw_buffer);
        init_valid_tcp_ipv6_base(hdr);
        hdr->tcp_src_port = slabflux::core::endian::host_to_network16(src_port);
        hdr->tcp_dst_port = slabflux::core::endian::host_to_network16(dst_port);
        
        uint32_t header_size = 74;
        uint32_t total_size = header_size + payload.size();
        hdr->ipv6_plen = slabflux::core::endian::host_to_network16(20 + payload.size());

        hdr->tcp_seq = slabflux::core::endian::host_to_network32(seq);
        hdr->tcp_ack = slabflux::core::endian::host_to_network32(ack);
        hdr->tcp_flags = flags;
        hdr->tcp_window = slabflux::core::endian::host_to_network16(65535);
        
        if (!payload.empty()) {
            std::memcpy(raw_buffer + header_size, payload.data(), payload.size());
        }
        
        uint16_t tcp_len = 20 + payload.size();
        uint32_t pseudo_sum = 0;
        const uint16_t* src16 = reinterpret_cast<const uint16_t*>(hdr->ipv6_src);
        const uint16_t* dst16 = reinterpret_cast<const uint16_t*>(hdr->ipv6_dst);
        for(int i=0; i<8; ++i) pseudo_sum += src16[i];
        for(int i=0; i<8; ++i) pseudo_sum += dst16[i];
        pseudo_sum += slabflux::core::endian::host_to_network16(6);
        pseudo_sum += slabflux::core::endian::host_to_network16(tcp_len);
        hdr->tcp_checksum = 0;
        hdr->tcp_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 54, tcp_len, pseudo_sum);
        
        struct rte_mbuf* mbuf = rte_pktmbuf_alloc(dummy_pool);
        if (mbuf) {
            char* mbuf_data = rte_pktmbuf_mtod(mbuf, char*);
            std::memcpy(mbuf_data, raw_buffer, total_size);
            mbuf->data_len = total_size; mbuf->pkt_len = total_size;
            gateway->on_raw_frame(mbuf_data, total_size, 1000, mbuf);
        } else { gateway->on_raw_frame(raw_buffer, total_size, 1000); }
    }

    void force_temporal_sweep(uint64_t current_time_ms) {
        for (int i = 0; i < 1024; ++i) { // 1048576 buckets / 1024 batch
            gateway->poll_temporal(current_time_ms);
        }
    }
};

TEST_F(PublicApiTrafficTest, SimpleTcpConnectionLifecycle) {
    // 1. Send SYN
    inject_tcp_frame(1000, 0, FLAG_SYN, "", 12345);
    
    // Gateway should generate a SYN-ACK cookie natively
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* syn_ack = reinterpret_cast<raw_tcp_ipv4_frame*>(
        rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*)
    );
    EXPECT_EQ(syn_ack->tcp_flags, FLAG_SYN | FLAG_ACK);
    uint32_t cookie = slabflux::core::endian::network_to_host32(syn_ack->tcp_seq);
    tx_ring.consume_n(1);
    
    // 2. Send ACK to complete handshake
    inject_tcp_frame(1001, cookie + 1, FLAG_ACK, "", 12345);
    
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    uint32_t conn_id = *accept_ring.get_peek_slot(0);
    accept_ring.consume_n(1);
    
    auto& tcb = gateway->get_tcb(conn_id);
    EXPECT_EQ(tcb.phase_mask, PHASE_ESTABLISHED);

    // 3. Send Data (In-order, contiguous stream)
    std::string payload = "Hello Public Gateway!";
    inject_tcp_frame(1001, cookie + 1, FLAG_ACK | FLAG_PSH, payload, 12345);
    
    std::string reconstructed(app.tcp_data.begin(), app.tcp_data.end());
    EXPECT_EQ(reconstructed, payload);
    EXPECT_EQ(tcb.rcv_nxt, 1001 + payload.size());
    
    // 4. Application validates ID routing
    EXPECT_EQ(app.active_conn_id, conn_id);
}

TEST_F(PublicApiTrafficTest, SimpleTcpIpv6ConnectionLifecycle) {
    // 1. Send SYN
    inject_tcp_ipv6_frame(1000, 0, FLAG_SYN, "", 12345);
    
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* syn_ack = reinterpret_cast<raw_tcp_ipv6_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*));
    EXPECT_EQ(syn_ack->tcp_flags, FLAG_SYN | FLAG_ACK);
    uint32_t cookie = slabflux::core::endian::network_to_host32(syn_ack->tcp_seq);
    tx_ring.consume_n(1);
    
    // 2. Send ACK
    inject_tcp_ipv6_frame(1001, cookie + 1, FLAG_ACK, "", 12345);
    
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    uint32_t conn_id = *accept_ring.get_peek_slot(0);
    accept_ring.consume_n(1);
    
    auto& tcb = gateway->get_tcb(conn_id);
    EXPECT_EQ(tcb.phase_mask, PHASE_ESTABLISHED);

    // 3. Send Payload
    std::string payload = "Hello IPv6 Mesh!";
    inject_tcp_ipv6_frame(1001, cookie + 1, FLAG_ACK | FLAG_PSH, payload, 12345);
    std::string reconstructed(app.tcp_data.begin(), app.tcp_data.end());
    EXPECT_EQ(reconstructed, payload);
}

TEST_F(PublicApiTrafficTest, FullTcpIpv6ConnectionLifecycle) {
    // 1. Send SYN
    inject_tcp_ipv6_frame(1000, 0, FLAG_SYN, "", 12345);
    
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* syn_ack = reinterpret_cast<raw_tcp_ipv6_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*));
    EXPECT_EQ(syn_ack->tcp_flags, FLAG_SYN | FLAG_ACK);
    uint32_t cookie = slabflux::core::endian::network_to_host32(syn_ack->tcp_seq);
    tx_ring.consume_n(1);
    
    // 2. Send ACK
    inject_tcp_ipv6_frame(1001, cookie + 1, FLAG_ACK, "", 12345);
    
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    uint32_t conn_id = *accept_ring.get_peek_slot(0);
    accept_ring.consume_n(1);
    
    auto& tcb = gateway->get_tcb(conn_id);
    EXPECT_EQ(tcb.phase_mask, PHASE_ESTABLISHED);

    // 3. Send Out-of-Order Data
    app.tcp_data.clear();
    std::string payload1 = "This is ";
    std::string payload2 = "out of order.";
    
    uint32_t seq1 = tcb.rcv_nxt;
    uint32_t seq2 = seq1 + payload1.size();

    inject_tcp_ipv6_frame(seq2, cookie + 1, FLAG_ACK | FLAG_PSH, payload2, 12345); // Send P2 first
    EXPECT_EQ(tcb.rcv_nxt, seq1); // Rcv Nxt shouldn't advance yet
    
    inject_tcp_ipv6_frame(seq1, cookie + 1, FLAG_ACK | FLAG_PSH, payload1, 12345); // Send P1 (Fills the gap)
    EXPECT_EQ(tcb.rcv_nxt, seq2 + payload2.size()); // Sequence mathematically verified and advanced

    std::string reconstructed(app.tcp_data.begin(), app.tcp_data.end());
    EXPECT_EQ(reconstructed, "This is out of order.");
}

TEST_F(PublicApiTrafficTest, FullTcpConnectionLifecycle) {
    // 1. Send SYN
    inject_tcp_frame(1000, 0, FLAG_SYN, "", 12345);
    
    // Gateway should generate a SYN-ACK cookie natively without allocating a TCB
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* syn_ack = reinterpret_cast<raw_tcp_ipv4_frame*>(
        rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*)
    );
    EXPECT_EQ(syn_ack->tcp_flags, FLAG_SYN | FLAG_ACK);
    uint32_t cookie = slabflux::core::endian::network_to_host32(syn_ack->tcp_seq);
    tx_ring.consume_n(1);
    
    // 2. Send ACK to complete handshake and rehydrate state
    inject_tcp_frame(1001, cookie + 1, FLAG_ACK, "", 12345);
    
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    uint32_t conn_id = *accept_ring.get_peek_slot(0);
    accept_ring.consume_n(1);
    
    auto& tcb = gateway->get_tcb(conn_id);
    EXPECT_EQ(tcb.phase_mask, PHASE_ESTABLISHED);

    // 3. Send Data (In-order, contiguous stream)
    std::string payload1 = "Hello, ";
    std::string payload2 = "World!";
    
    inject_tcp_frame(1001, cookie + 1, FLAG_ACK | FLAG_PSH, payload1, 12345);
    inject_tcp_frame(1001 + payload1.size(), cookie + 1, FLAG_ACK | FLAG_PSH, payload2, 12345);
    
    // Ensure L7 mock app received it sequentially
    std::string reconstructed(app.tcp_data.begin(), app.tcp_data.end());
    EXPECT_EQ(reconstructed, "Hello, World!");
    EXPECT_EQ(tcb.rcv_nxt, 1001 + payload1.size() + payload2.size());

    // 4. Send Out-of-Order Data
    std::string payload3 = "This is ";
    std::string payload4 = "out of order.";
    
    uint32_t seq3 = tcb.rcv_nxt;
    uint32_t seq4 = seq3 + payload3.size();

    inject_tcp_frame(seq4, cookie + 1, FLAG_ACK | FLAG_PSH, payload4, 12345); // Send P4 first
    EXPECT_EQ(tcb.rcv_nxt, seq3); // Rcv Nxt shouldn't advance yet (Matrix stores it safely)
    
    inject_tcp_frame(seq3, cookie + 1, FLAG_ACK | FLAG_PSH, payload3, 12345); // Send P3 (Fills the gap)
    EXPECT_EQ(tcb.rcv_nxt, seq4 + payload4.size()); // Sequence mathematically verified and advanced

    reconstructed.assign(app.tcp_data.begin(), app.tcp_data.end());
    EXPECT_EQ(reconstructed, "Hello, World!This is out of order.");

    // 5. Connection Teardown (FIN)
    inject_tcp_frame(tcb.rcv_nxt, cookie + 1, FLAG_ACK | FLAG_FIN);
    EXPECT_EQ(tcb.phase_mask, PHASE_CLOSE_WAIT);
}

TEST_F(PublicApiTrafficTest, UdpDatagramHandling) {
    alignas(64) char raw_buffer[128];
    std::memset(raw_buffer, 0, sizeof(raw_buffer));
    auto* hdr = reinterpret_cast<raw_udp_ipv4_frame*>(raw_buffer);
    
    std::memcpy(hdr->dest_mac, "\x00\x11\x22\x33\x44\x55", 6);
    std::memcpy(hdr->src_mac, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    hdr->eth_type = slabflux::core::endian::host_to_network16(0x0800);
    hdr->ip_ihl_ver = 0x45;
    hdr->ip_protocol = 17; // UDP
    hdr->ip_src = slabflux::core::endian::host_to_network32(0x08080808);
    hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A000001);
    
    std::string payload = "UDP Payload Datagram Test";
    uint32_t total_len = 42 + payload.size();
    
    hdr->ip_len = slabflux::core::endian::host_to_network16(total_len - 14);
    hdr->udp_src_port = slabflux::core::endian::host_to_network16(5000);
    hdr->udp_dst_port = slabflux::core::endian::host_to_network16(8080);
    hdr->udp_length = slabflux::core::endian::host_to_network16(8 + payload.size());
    hdr->udp_checksum = 0;

    std::memcpy(raw_buffer + 42, payload.data(), payload.size());
    hdr->ip_checksum = 0;
    hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
    
    gateway->on_raw_frame(raw_buffer, total_len, 1000);

    // Verify L7 mock app natively received the intact UDP vector
    std::string reconstructed(app.udp_data.begin(), app.udp_data.end());
    EXPECT_EQ(reconstructed, payload);
}

TEST_F(PublicApiTrafficTest, UdpIpv6DatagramHandling) {
    alignas(64) char raw_buffer[128];
    std::memset(raw_buffer, 0, sizeof(raw_buffer));
    auto* hdr = reinterpret_cast<raw_udp_ipv6_frame*>(raw_buffer);
    
    std::memcpy(hdr->dest_mac, "\x00\x11\x22\x33\x44\x55", 6);
    std::memcpy(hdr->src_mac, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    hdr->eth_type = slabflux::core::endian::host_to_network16(0x86DD);
    hdr->ipv6_flow = slabflux::core::endian::host_to_network32(0x60000000);
    hdr->ipv6_nxt = 17; // UDP
    hdr->ipv6_hlim = 64;
    
    hdr->ipv6_src[0] = slabflux::core::endian::host_to_network64(0x20010db800000000ULL);
    hdr->ipv6_src[1] = slabflux::core::endian::host_to_network64(0x0000000000000002ULL);
    hdr->ipv6_dst[0] = slabflux::core::endian::host_to_network64(0xfe80000000000000ULL);
    hdr->ipv6_dst[1] = slabflux::core::endian::host_to_network64(0x0000000000000001ULL);
    
    std::string payload = "IPv6 UDP Payload";
    uint32_t total_len = 54 + 8 + payload.size();
    hdr->ipv6_plen = slabflux::core::endian::host_to_network16(8 + payload.size());
    hdr->udp_src_port = slabflux::core::endian::host_to_network16(5000);
    hdr->udp_dst_port = slabflux::core::endian::host_to_network16(8080);
    hdr->udp_length = slabflux::core::endian::host_to_network16(8 + payload.size());
    hdr->udp_checksum = 0; // RFC 768 exception OK over IPv6 for internal test harnesses natively, but typically IPv6 enforces it. We use 0.

    std::memcpy(raw_buffer + 62, payload.data(), payload.size());
    gateway->on_raw_frame(raw_buffer, total_len, 1000);

    std::string reconstructed(app.udp_data.begin(), app.udp_data.end());
    EXPECT_EQ(reconstructed, payload);
}

// ============================================================================
// EXTENSIVE FUNCTIONAL TRAFFIC & STATE MACHINE VALIDATION
// ============================================================================

TEST_F(PublicApiTrafficTest, TcpHandshakeWithOptions) {
    alignas(64) char raw_buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(raw_buffer);
    init_valid_tcp_base(hdr);
    hdr->tcp_seq = slabflux::core::endian::host_to_network32(2000);
    hdr->tcp_src_port = slabflux::core::endian::host_to_network16(12346);
    
    hdr->tcp_data_offset = 0x90; // 36 bytes (20 Base + 16 Options)
    uint8_t* opt = reinterpret_cast<uint8_t*>(raw_buffer) + 54;
    
    // MSS 1460 Option
    opt[0] = 2; opt[1] = 4; opt[2] = 0x05; opt[3] = 0xB4;
    // WScale 5 Option
    opt[4] = 3; opt[5] = 3; opt[6] = 5;
    opt[7] = 1; // NOP
    opt[8] = 1; opt[9] = 1; // NOP NOP
    opt[10] = 8; opt[11] = 10; // TS Option
    *reinterpret_cast<uint32_t*>(&opt[12]) = slabflux::core::endian::host_to_network32(100);
    *reinterpret_cast<uint32_t*>(&opt[16]) = slabflux::core::endian::host_to_network32(0);
    
    hdr->ip_len = slabflux::core::endian::host_to_network16(56); // 20 IP + 36 TCP
    hdr->ip_checksum = 0;
    hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
    
    uint32_t pseudo_sum = 0;
    uint32_t src = hdr->ip_src;
    uint32_t dst = hdr->ip_dst;
    pseudo_sum += (src & 0xFFFF) + (src >> 16);
    pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
    pseudo_sum += slabflux::core::endian::host_to_network16(6);
    pseudo_sum += slabflux::core::endian::host_to_network16(36);
    hdr->tcp_checksum = 0;
    hdr->tcp_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 34, 36, pseudo_sum);

    gateway->on_raw_frame(raw_buffer, 70, 1000); // 14 Eth + 56 IP
    
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* syn_ack = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*));
    EXPECT_EQ(syn_ack->tcp_flags, FLAG_SYN | FLAG_ACK);
    uint32_t cookie = slabflux::core::endian::network_to_host32(syn_ack->tcp_seq);
    tx_ring.consume_n(1);
    
    // Complete handshake with ACK
    inject_tcp_frame(2001, cookie + 1, FLAG_ACK, "", 12346);
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    uint32_t conn_id = *accept_ring.get_peek_slot(0);
    accept_ring.consume_n(1);
    
    auto& tcb = gateway->get_tcb(conn_id);
    EXPECT_EQ(tcb.phase_mask, PHASE_ESTABLISHED);
    EXPECT_EQ(tcb.remote_mss, 1460);
    EXPECT_EQ(tcb.snd_wscale, 0); // Not sent in ACK, so not parsed
}

TEST_F(PublicApiTrafficTest, TcpOutboundMssFragmentation) {
    alignas(64) char raw_buffer[128];
    auto* syn = reinterpret_cast<raw_tcp_ipv4_frame*>(raw_buffer);
    init_valid_tcp_base(syn);
    syn->tcp_seq = slabflux::core::endian::host_to_network32(3000);
    syn->tcp_src_port = slabflux::core::endian::host_to_network16(12347);
    syn->tcp_data_offset = 0x60;
    uint8_t* opt = reinterpret_cast<uint8_t*>(raw_buffer) + 54;
    opt[0] = 2; opt[1] = 4; opt[2] = 0x05; opt[3] = 0xB4; // Advertise 1460 MSS
    syn->ip_len = slabflux::core::endian::host_to_network16(44);
    syn->ip_checksum = 0;
    syn->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(syn) + 14, 20, 0);
    uint32_t pseudo_sum = 0;
    uint32_t src = syn->ip_src;
    uint32_t dst = syn->ip_dst;
    pseudo_sum += (src & 0xFFFF) + (src >> 16);
    pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
    pseudo_sum += slabflux::core::endian::host_to_network16(6);
    pseudo_sum += slabflux::core::endian::host_to_network16(24);
    syn->tcp_checksum = 0;
    syn->tcp_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(syn) + 34, 24, pseudo_sum);

    gateway->on_raw_frame(raw_buffer, 58, 1000);

    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    uint32_t cookie = slabflux::core::endian::network_to_host32(reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*))->tcp_seq);
    tx_ring.consume_n(1);
    
    inject_tcp_frame(3001, cookie + 1, FLAG_ACK, "", 12347);
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    uint32_t conn_id = *accept_ring.get_peek_slot(0);
    accept_ring.consume_n(1);
    
    auto& tcb = gateway->get_tcb(conn_id);
    virtual_tcp_socket sock(&tcb);
    
    std::string large_payload(4000, 'X');
    ssize_t sent = sock.send(large_payload.data(), large_payload.size());
    EXPECT_EQ(sent, 4000);
    
    // 4000 bytes with 1460 MSS = 1460 + 1460 + 1080
    ASSERT_EQ(tx_ring.available_to_peek(), 3);
    
    auto* seg1 = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, char*));
    auto* seg2 = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(1)->mbuf, char*));
    auto* seg3 = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(2)->mbuf, char*));
    
    EXPECT_EQ(slabflux::core::endian::network_to_host16(seg1->ip_len), 1460 + 40);
    EXPECT_EQ(slabflux::core::endian::network_to_host16(seg2->ip_len), 1460 + 40);
    EXPECT_EQ(slabflux::core::endian::network_to_host16(seg3->ip_len), 1080 + 40);
    
    tx_ring.consume_n(3);
}

TEST_F(PublicApiTrafficTest, TcpFastRetransmit) {
    alignas(64) char raw_buffer[128];
    auto* syn = reinterpret_cast<raw_tcp_ipv4_frame*>(raw_buffer);
    init_valid_tcp_base(syn);
    syn->tcp_seq = slabflux::core::endian::host_to_network32(4000);
    syn->tcp_src_port = slabflux::core::endian::host_to_network16(12348);
    syn->tcp_data_offset = 0x60;
    uint8_t* opt = reinterpret_cast<uint8_t*>(raw_buffer) + 54;
    opt[0] = 2; opt[1] = 4; opt[2] = 0x05; opt[3] = 0xB4; 
    syn->ip_len = slabflux::core::endian::host_to_network16(44);
    syn->ip_checksum = 0;
    syn->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(syn) + 14, 20, 0);
    uint32_t pseudo_sum = 0;
    uint32_t src = syn->ip_src;
    uint32_t dst = syn->ip_dst;
    pseudo_sum += (src & 0xFFFF) + (src >> 16);
    pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
    pseudo_sum += slabflux::core::endian::host_to_network16(6);
    pseudo_sum += slabflux::core::endian::host_to_network16(24);
    syn->tcp_checksum = 0;
    syn->tcp_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(syn) + 34, 24, pseudo_sum);

    gateway->on_raw_frame(raw_buffer, 58, 1000);

    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    uint32_t cookie = slabflux::core::endian::network_to_host32(reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*))->tcp_seq);
    tx_ring.consume_n(1);
    
    inject_tcp_frame(4001, cookie + 1, FLAG_ACK, "", 12348);
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    uint32_t conn_id = *accept_ring.get_peek_slot(0);
    accept_ring.consume_n(1);
    
    auto& tcb = gateway->get_tcb(conn_id);
    tcb.snd_wnd = 65535; // Ensure window is fully open
    tcb.remote_mss = 1460;
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> unacked_ring;
    tcb.tx_unacked_ring = &unacked_ring;
    virtual_tcp_socket sock(&tcb);
    
    std::string payload(1460 * 4, 'X'); // Send 4 segments
    sock.send(payload.data(), payload.size());
    ASSERT_EQ(tx_ring.available_to_peek(), 4);
    tx_ring.consume_n(4); // Discard to simulate transmission
    
    // Simulating sequence: we sent 4 packets starting at `cookie + 1`.
    // Peer received 2, 3, 4, but dropped 1.
    // It responds with DUP ACKs acknowledging up to `cookie + 1` (the missing seq).
    inject_tcp_frame(4001, cookie + 1, FLAG_ACK, "", 12348); // DUP 1
    inject_tcp_frame(4001, cookie + 1, FLAG_ACK, "", 12348); // DUP 2
    inject_tcp_frame(4001, cookie + 1, FLAG_ACK, "", 12348); // DUP 3 (Trigger Fast Retransmit natively)
    
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* rtx = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, char*));
    EXPECT_EQ(slabflux::core::endian::network_to_host32(rtx->tcp_seq), cookie + 1);
    tx_ring.consume_n(1);
}

TEST_F(PublicApiTrafficTest, TcpKeepAliveTemporalSweep) {
    inject_tcp_frame(5000, 0, FLAG_SYN, "", 12349);
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    uint32_t cookie = slabflux::core::endian::network_to_host32(reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*))->tcp_seq);
    tx_ring.consume_n(1);
    
    inject_tcp_frame(5001, cookie + 1, FLAG_ACK, "", 12349);
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    uint32_t conn_id = *accept_ring.get_peek_slot(0);
    accept_ring.consume_n(1);
    
    auto& tcb = gateway->get_tcb(conn_id);
    virtual_tcp_socket sock(&tcb);
    sock.set_keepalive(true);
    
    // Fast forward temporal clock by > 2 hours (7,200,000 ms)
    force_temporal_sweep(1000); // Arm
    force_temporal_sweep(7202000); // Trigger
    
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* probe = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, char*));
    EXPECT_EQ(probe->tcp_flags, FLAG_ACK);
    // RFC 1122 Keep-Alive Probe sequence must equal snd_nxt - 1
    EXPECT_EQ(slabflux::core::endian::network_to_host32(probe->tcp_seq), tcb.snd_nxt - 1);
    tx_ring.consume_n(1);
}

TEST_F(PublicApiTrafficTest, TcpHalfClosedLifecycle) {
    inject_tcp_frame(6000, 0, FLAG_SYN, "", 12350);
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    uint32_t cookie = slabflux::core::endian::network_to_host32(reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*))->tcp_seq);
    tx_ring.consume_n(1);
    
    inject_tcp_frame(6001, cookie + 1, FLAG_ACK, "", 12350);
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    uint32_t conn_id = *accept_ring.get_peek_slot(0);
    accept_ring.consume_n(1);
    
    auto& tcb = gateway->get_tcb(conn_id);
    
    // Remote peer sends FIN
    inject_tcp_frame(6001, cookie + 1, FLAG_ACK | FLAG_FIN, "", 12350);
    EXPECT_EQ(tcb.phase_mask, PHASE_CLOSE_WAIT);
    
    // Gateway auto-ACKs the incoming FIN
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    tx_ring.consume_n(1);
    
    // Application Layer can still transmit data over the Half-Closed Socket!
    virtual_tcp_socket sock(&tcb);
    ssize_t sent = sock.send("Last Words", 10);
    EXPECT_EQ(sent, 10);
    
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* data_pkt = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, char*));
    EXPECT_EQ(data_pkt->tcp_flags, FLAG_ACK | FLAG_PSH);
    tx_ring.consume_n(1);
    
    // Application Layer finally finishes its transmission and closes the socket
    sock.close();
    EXPECT_EQ(tcb.phase_mask, PHASE_LAST_ACK);
    
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* fin_pkt = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, char*));
    EXPECT_EQ(fin_pkt->tcp_flags, FLAG_ACK | FLAG_FIN);
    tx_ring.consume_n(1);
}

TEST_F(PublicApiTrafficTest, UdpOutboundWithArpResolution) {
    virtual_udp_socket<public_tcp_gateway<TrafficMockApp>> sock(*gateway, 12345);
    
    // 1. Send UDP - expect Gateway to pause payload and queue an ARP Request natively
    bool sent = sock.send_to(slabflux::core::endian::host_to_network32(0x0A000005), 53, 12345, "test", 4);
    EXPECT_FALSE(sent); // Blocked, waiting for ARP resolution
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    
    auto* arp_req = reinterpret_cast<raw_arp_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, char*));
    EXPECT_EQ(arp_req->eth_type, slabflux::core::endian::host_to_network16(0x0806));
    EXPECT_EQ(arp_req->opcode, slabflux::core::endian::host_to_network16(1)); // ARP Request
    tx_ring.consume_n(1);
    
    // 2. Inject incoming ARP Reply
    alignas(64) char arp_buf[64] = {0};
    auto* arp_reply = reinterpret_cast<raw_arp_frame*>(arp_buf);
    arp_reply->eth_type = slabflux::core::endian::host_to_network16(0x0806);
    arp_reply->opcode = slabflux::core::endian::host_to_network16(2); // ARP Reply
    arp_reply->sender_ip = slabflux::core::endian::host_to_network32(0x0A000005);
    std::memcpy(arp_reply->sender_mac, "\x12\x22\x33\x44\x55\x66", 6); // Valid Unicast MAC
    arp_reply->target_ip = slabflux::core::endian::host_to_network32(0x0A000001);
    std::memcpy(arp_reply->target_mac, "\x00\x11\x22\x33\x44\x55", 6);
    
    gateway->on_raw_frame(arp_buf, 64, 1000); // Gateway processes ARP reply and updates L2 cache
    
    // 3. Send UDP again - expect actual UDP payload to securely pass
    sent = sock.send_to(slabflux::core::endian::host_to_network32(0x0A000005), 53, 12345, "test", 4);
    EXPECT_TRUE(sent);
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    
    auto* udp_pkt = reinterpret_cast<raw_udp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, char*));
    EXPECT_EQ(udp_pkt->ip_protocol, 17);
    EXPECT_EQ(udp_pkt->udp_length, slabflux::core::endian::host_to_network16(12)); // 8 Header + 4 Payload
    EXPECT_EQ(std::memcmp(udp_pkt->dest_mac, "\x12\x22\x33\x44\x55\x66", 6), 0); // Correct MAC attached
    tx_ring.consume_n(1);
}

TEST_F(PublicApiTrafficTest, UdpIpv6OutboundWithNdpResolution) {
    virtual_udp_socket<public_tcp_gateway<TrafficMockApp>> sock(*gateway, 12345);
    
    uint64_t target_ipv6[2] = {slabflux::core::endian::host_to_network64(0x20010db800000000ULL), slabflux::core::endian::host_to_network64(0x0000000000000002ULL)};
    
    // 1. Send UDP - expect Gateway to pause payload and queue an NDP Neighbor Solicitation natively
    bool sent = sock.send_to_ipv6(target_ipv6, 53, 12345, "test", 4);
    EXPECT_FALSE(sent); // Blocked, waiting for NDP resolution
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    
    auto* ndp_req = reinterpret_cast<raw_tcp_ipv6_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, char*));
    EXPECT_EQ(ndp_req->eth_type, slabflux::core::endian::host_to_network16(0x86DD));
    const char* raw = reinterpret_cast<const char*>(ndp_req);
    EXPECT_EQ(raw[54], static_cast<char>(135)); // ICMPv6 Neighbor Solicitation (Type 135)
    tx_ring.consume_n(1);
    
    // 2. Inject incoming NDP Neighbor Advertisement
    alignas(64) char ndp_buf[128] = {0};
    auto* ndp_reply = reinterpret_cast<raw_tcp_ipv6_frame*>(ndp_buf);
    init_valid_tcp_ipv6_base(ndp_reply);
    ndp_reply->ipv6_nxt = 58; // ICMPv6
    ndp_buf[54] = static_cast<char>(136); // Type: Neighbor Advertisement
    ndp_buf[55] = 0; // Code
    ndp_buf[58] = 0x60; // Flags (Solicited, Override)
    std::memcpy(&ndp_buf[62], target_ipv6, 16); // Target Address
    ndp_buf[78] = 2; // Option: Target Link-Layer Address
    ndp_buf[79] = 1; // Length 8
    std::memcpy(&ndp_buf[80], "\x12\x22\x33\x44\x55\x66", 6);
    ndp_reply->ipv6_plen = slabflux::core::endian::host_to_network16(32);
    
    gateway->on_raw_frame(ndp_buf, 86, 1000); 
    
    // 3. Send UDP again
    sent = sock.send_to_ipv6(target_ipv6, 53, 12345, "test", 4);
    EXPECT_TRUE(sent);
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    tx_ring.consume_n(1);
}

TEST_F(PublicApiTrafficTest, TcpZeroWindowProbing) {
    inject_tcp_frame(7000, 0, FLAG_SYN, "", 12351);
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    uint32_t cookie = slabflux::core::endian::network_to_host32(reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*))->tcp_seq);
    tx_ring.consume_n(1);
    
    // Peer completes handshake but advertises a 0-byte window
    alignas(64) char raw_buffer[128];
    auto* ack = reinterpret_cast<raw_tcp_ipv4_frame*>(raw_buffer);
    init_valid_tcp_base(ack);
    ack->tcp_seq = slabflux::core::endian::host_to_network32(7001);
    ack->tcp_ack = slabflux::core::endian::host_to_network32(cookie + 1);
    ack->tcp_flags = FLAG_ACK;
    ack->tcp_window = 0; // Zero Window!
    ack->tcp_src_port = slabflux::core::endian::host_to_network16(12351);
    
    ack->ip_checksum = 0;
    ack->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(ack) + 14, 20, 0);
    uint16_t tcp_len = 20;
    uint32_t pseudo_sum = 0;
    uint32_t src = ack->ip_src;
    uint32_t dst = ack->ip_dst;
    pseudo_sum += (src & 0xFFFF) + (src >> 16);
    pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
    pseudo_sum += slabflux::core::endian::host_to_network16(6);
    pseudo_sum += slabflux::core::endian::host_to_network16(tcp_len);
    ack->tcp_checksum = 0;
    ack->tcp_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(ack) + 34, tcp_len, pseudo_sum);

    gateway->on_raw_frame(raw_buffer, 54, 1000);
    
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    uint32_t conn_id = *accept_ring.get_peek_slot(0);
    accept_ring.consume_n(1);
    
    auto& tcb = gateway->get_tcb(conn_id);
    virtual_tcp_socket sock(&tcb);
    
    // Send should block natively
    ssize_t sent = sock.send("Test", 4);
    EXPECT_EQ(sent, 0); // Blocked
    EXPECT_TRUE(tcb.temporal_flags & TEMP_FLAG_ZWP_ACTIVE);
    
    force_temporal_sweep(2000); // 1s later, timer expires
    force_temporal_sweep(3000); // Trigger probe natively
    
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* probe = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, char*));
    EXPECT_EQ(probe->tcp_flags, FLAG_ACK); // Window probe
    EXPECT_EQ(slabflux::core::endian::network_to_host32(probe->tcp_seq), tcb.snd_nxt - 1);
    tx_ring.consume_n(1);
}

TEST_F(PublicApiTrafficTest, TcpSimultaneousOpen) {
    uint32_t conn_id = gateway->connect_outbound(
        slabflux::core::endian::host_to_network32(0x08080808), 80
    );
    
    auto& tcb = gateway->get_tcb(conn_id);
    EXPECT_EQ(tcb.phase_mask, PHASE_SYN_SENT);
    
    // Simulate peer sending SYN simultaneously
    uint16_t local_port = slabflux::core::endian::network_to_host16(tcb.local_port);
    inject_tcp_frame(5000, 0, FLAG_SYN, "", 80, local_port); 
    
    EXPECT_EQ(tcb.phase_mask, PHASE_SYN_RCVD);
    ASSERT_EQ(tx_ring.available_to_peek(), 1); // Should have generated SYN-ACK
    auto* syn_ack = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*));
    EXPECT_EQ(syn_ack->tcp_flags, FLAG_SYN | FLAG_ACK | FLAG_ECE | FLAG_CWR);
    tx_ring.consume_n(1);
}

TEST_F(PublicApiTrafficTest, TcpPawsRejection) {
    inject_tcp_frame(8000, 0, FLAG_SYN, "", 12352);
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    uint32_t cookie = slabflux::core::endian::network_to_host32(reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*))->tcp_seq);
    tx_ring.consume_n(1);
    
    inject_tcp_frame(8001, cookie + 1, FLAG_ACK, "", 12352);
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    uint32_t conn_id = *accept_ring.get_peek_slot(0);
    accept_ring.consume_n(1);
    
    auto& tcb = gateway->get_tcb(conn_id);
    tcb.ts_permitted = 1;
    tcb.ts_recent = 50000;
    
    alignas(64) char raw_buffer[128];
    auto* data_frame = reinterpret_cast<raw_tcp_ipv4_frame*>(raw_buffer);
    init_valid_tcp_base(data_frame);
    data_frame->tcp_seq = slabflux::core::endian::host_to_network32(8001);
    data_frame->tcp_ack = slabflux::core::endian::host_to_network32(cookie + 1);
    data_frame->tcp_flags = FLAG_ACK | FLAG_PSH;
    data_frame->tcp_src_port = slabflux::core::endian::host_to_network16(12352);
    
    data_frame->tcp_data_offset = 0x80; // 32 bytes (20 + 12 opt for TS)
    uint8_t* opt = reinterpret_cast<uint8_t*>(raw_buffer) + 54;
    opt[0] = 0x01; opt[1] = 0x01;
    opt[2] = 0x08; opt[3] = 0x0A;
    // Inject old timestamp: 10000 (which is < ts_recent 50000)
    *reinterpret_cast<uint32_t*>(&opt[4]) = slabflux::core::endian::host_to_network32(10000); 
    *reinterpret_cast<uint32_t*>(&opt[8]) = slabflux::core::endian::host_to_network32(0);
    
    data_frame->ip_len = slabflux::core::endian::host_to_network16(62); // 20 IP + 32 TCP + 10 Payload
    std::memcpy(raw_buffer + 86, "PAWS_DROP!", 10);
    
    data_frame->ip_checksum = 0;
    data_frame->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(data_frame) + 14, 20, 0);
    uint16_t tcp_len = 42; // 32 header + 10 payload
    uint32_t pseudo_sum = 0;
    uint32_t src = data_frame->ip_src;
    uint32_t dst = data_frame->ip_dst;
    pseudo_sum += (src & 0xFFFF) + (src >> 16);
    pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
    pseudo_sum += slabflux::core::endian::host_to_network16(6);
    pseudo_sum += slabflux::core::endian::host_to_network16(tcp_len);
    data_frame->tcp_checksum = 0;
    data_frame->tcp_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(data_frame) + 34, tcp_len, pseudo_sum);

    gateway->on_raw_frame(raw_buffer, 76, 1000);
    
    // Must be silently dropped due to PAWS, and a challenge ACK should be queued
    EXPECT_EQ(tcb.rcv_nxt, 8001); // Did not advance
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* challenge = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, char*));
    EXPECT_EQ(challenge->tcp_flags, FLAG_ACK);
    tx_ring.consume_n(1);
}

TEST_F(PublicApiTrafficTest, UdpBroadcastRouting) {
    virtual_udp_socket<public_tcp_gateway<TrafficMockApp>> sock(*gateway, 12345);
    
    // Send to global broadcast 255.255.255.255
    bool sent = sock.send_to(0xFFFFFFFF, 53, 12345, "broadcast", 9);
    
    EXPECT_TRUE(sent); // No ARP needed!
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    
    auto* udp_pkt = reinterpret_cast<raw_udp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, char*));
    EXPECT_EQ(udp_pkt->ip_protocol, 17);
    EXPECT_EQ(std::memcmp(udp_pkt->dest_mac, "\xFF\xFF\xFF\xFF\xFF\xFF", 6), 0); // Broadcast MAC
    tx_ring.consume_n(1);
}
