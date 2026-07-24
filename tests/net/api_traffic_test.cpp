/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 * @file api_traffic_test.cpp
 * @brief Simple functional testing for the internal tcp_gateway processing standard TCP/UDP traffic.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include <string>

#include <rte_mbuf.h>

// Mock DPDK Mempool Allocations for Unit Testing
inline struct rte_mbuf* mock_mbuf_alloc_api(struct rte_mempool*) {
    char* mem = new char[sizeof(struct rte_mbuf) + 2048];
    std::memset(mem, 0, sizeof(struct rte_mbuf) + 2048);
    struct rte_mbuf* m = reinterpret_cast<struct rte_mbuf*>(mem);
    m->buf_addr = mem + sizeof(struct rte_mbuf);
    m->data_off = 0;
    m->pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    rte_mbuf_refcnt_set(m, 1);
    return m;
}

inline void mock_mbuf_free_api(struct rte_mbuf* m) {
    if (m) {
        if (rte_mbuf_refcnt_read(m) <= 1) delete[] reinterpret_cast<char*>(m);
        else rte_mbuf_refcnt_update(m, -1);
    }
}

#undef rte_pktmbuf_alloc
#undef rte_pktmbuf_free
#define rte_pktmbuf_alloc mock_mbuf_alloc_api
#define rte_pktmbuf_free mock_mbuf_free_api

#include "slabflux/net/tcp_gateway.hpp"
#include "slabflux/net/virtual_tcp_socket.hpp"
#include "slabflux/net/virtual_udp_socket.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"

using namespace slabflux::net;

struct ApiTrafficMockApp {
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

class ApiTrafficTest : public ::testing::Test {
protected:
    ApiTrafficMockApp app;
    tcp_gateway<ApiTrafficMockApp>* gateway;
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    slabflux::core::spsc_ring_conduit<uint32_t, 1024> accept_ring;
    struct rte_mempool* dummy_pool;

    void SetUp() override {
        gateway = new tcp_gateway<ApiTrafficMockApp>(app);
        dummy_pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
        gateway->bind_conduits(&tx_ring, &accept_ring);
        gateway->bind_mempool(dummy_pool);
        tcp_syn_cookie::seed_keys();
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

    void inject_tcp_frame(uint32_t conn_id, uint32_t seq, uint32_t ack, uint8_t flags, const std::string& payload = "", uint16_t src_port = 12345, uint16_t dst_port = 80) {
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
            gateway->on_raw_frame(mbuf_data, total_size, conn_id, mbuf);
        } else {
            gateway->on_raw_frame(raw_buffer, total_size, conn_id);
        }
    }
};

TEST_F(ApiTrafficTest, InternalTcpConnectionLifecycle) {
    uint32_t conn_id = 42;
    
    auto& tcb = gateway->get_tcb(conn_id);
    tcb.phase_mask = PHASE_LISTEN;
    tcb.local_port = slabflux::core::endian::host_to_network16(80);

    // 1. Send SYN
    inject_tcp_frame(conn_id, 1000, 0, FLAG_SYN, "", 12345);
    
    // Gateway should generate a SYN-ACK cookie natively
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* syn_ack = reinterpret_cast<raw_tcp_ipv4_frame*>(
        rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*)
    );
    EXPECT_EQ(syn_ack->tcp_flags, FLAG_SYN | FLAG_ACK);
    uint32_t cookie = slabflux::core::endian::network_to_host32(syn_ack->tcp_seq);
    tx_ring.consume_n(1);
    
    // 2. Send ACK to complete handshake
    inject_tcp_frame(conn_id, 1001, cookie + 1, FLAG_ACK, "", 12345);
    
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    EXPECT_EQ(*accept_ring.get_peek_slot(0), conn_id);
    accept_ring.consume_n(1);
    
    EXPECT_EQ(tcb.phase_mask, PHASE_ESTABLISHED);

    // 3. Send Data (In-order, contiguous stream)
    std::string payload = "Hello Internal Mesh!";
    inject_tcp_frame(conn_id, 1001, cookie + 1, FLAG_ACK | FLAG_PSH, payload, 12345);
    
    std::string reconstructed(app.tcp_data.begin(), app.tcp_data.end());
    EXPECT_EQ(reconstructed, payload);
    EXPECT_EQ(tcb.rcv_nxt, 1001 + payload.size());
    
    // 4. Application validates ID routing
    EXPECT_EQ(app.active_conn_id, conn_id);
}