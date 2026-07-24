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
 * @file public_api_security_test.cpp
 * @brief Rigorous Security & Integrity Bounds Testing for the Public Gateway.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdlib>

#include <rte_mbuf.h>

// Mock DPDK Mempool Allocations for Unit Testing
inline struct rte_mbuf* mock_mbuf_alloc_sec(struct rte_mempool*) {
    char* ptr = new char[sizeof(struct rte_mbuf) + 2048];
    std::memset(ptr, 0, sizeof(struct rte_mbuf) + 2048);
    struct rte_mbuf* m = reinterpret_cast<struct rte_mbuf*>(ptr);
    m->buf_addr = ptr + sizeof(struct rte_mbuf);
    m->data_off = 128; // Standard DPDK Headroom (RTE_PKTMBUF_HEADROOM)
    m->pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF); // Required by rte_pktmbuf_free
    rte_mbuf_refcnt_set(m, 1);
    return m;
}

inline void mock_mbuf_free_sec(struct rte_mbuf* m) {
    if (m) {
        if (rte_mbuf_refcnt_read(m) <= 1) delete[] reinterpret_cast<char*>(m);
        else rte_mbuf_refcnt_update(m, -1);
    }
}

#undef rte_pktmbuf_alloc
#undef rte_pktmbuf_free
#define rte_pktmbuf_alloc mock_mbuf_alloc_sec
#define rte_pktmbuf_free mock_mbuf_free_sec

#include "slabflux/net/public_tcp_gateway.hpp"
#include "slabflux/net/virtual_tcp_socket.hpp"
#include "slabflux/net/virtual_udp_socket.hpp"
#include "slabflux/net/raw_tcp_ipv4_frame.hpp"
#include "slabflux/net/raw_udp_ipv4_frame.hpp"
#include "slabflux/net/raw_udp_ipv6_frame.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"

using namespace slabflux::net;

struct MockDefragmenter {
    int tcp_stream_calls = 0;
    int udp_datagram_calls = 0;

    template <typename FrameType>
    void on(const FrameType&) {
        tcp_stream_calls++;
    }
    template <typename FrameType>
    bool on_udp(const FrameType&) {
        udp_datagram_calls++;
        return true;
    }
    template <typename FrameType>
    bool on_udp_ipv6(const FrameType&) {
        udp_datagram_calls++;
        return true;
    }
};

class PublicGatewaySecurityTest : public ::testing::Test {
protected:
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> egress_ring;
    slabflux::core::spsc_ring_conduit<uint32_t, 1024> accept_ring;
    MockDefragmenter defrag;
    public_tcp_gateway<MockDefragmenter> gateway{defrag};

    void SetUp() override {
        gateway.bind_conduits(&egress_ring, &accept_ring);
        gateway.bind_mempool(reinterpret_cast<struct rte_mempool*>(0xDEADBEEF));
        uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        gateway.set_local_identity(slabflux::core::endian::host_to_network32(0x0A000001), mac,
                                   slabflux::core::endian::host_to_network32(0xFFFFFF00)); // 10.0.0.1
                                   
        uint64_t local_ipv6[2] = { slabflux::core::endian::host_to_network64(0xfe80000000000000ULL), slabflux::core::endian::host_to_network64(0x0000000000000001ULL) };
        gateway.set_local_identity_v6(local_ipv6);

        tcp_syn_cookie::seed_keys();
        for (auto& entry : tcp_syn_cookie::replay_cache) {
            entry.store(0, std::memory_order_relaxed);
        }
    }

    // Helper to initialize a perfectly valid L2/L3/L4 baseline frame
    void init_valid_tcp_base(raw_tcp_ipv4_frame* hdr) {
        std::memset(hdr, 0, sizeof(raw_tcp_ipv4_frame));
        hdr->eth_type = slabflux::core::endian::host_to_network16(0x0800);
        hdr->ip_ihl_ver = 0x45;
        hdr->ip_len = slabflux::core::endian::host_to_network16(40);
        hdr->ip_ttl = 64;
        hdr->ip_protocol = 6;
        hdr->ip_src = slabflux::core::endian::host_to_network32(0x08080808); // 8.8.8.8
        hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A000001); // 10.0.0.1
        hdr->ip_checksum = slabflux::net::tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
        hdr->tcp_src_port = slabflux::core::endian::host_to_network16(12345);
        hdr->tcp_dst_port = slabflux::core::endian::host_to_network16(80);
        hdr->tcp_data_offset = 0x50; // 20 bytes
        hdr->tcp_flags = FLAG_SYN;
        hdr->tcp_window = slabflux::core::endian::host_to_network16(65535);
        // Ensure Unicast Destination MAC
        hdr->dest_mac[0] = 0x00;
        std::memcpy(hdr->dest_mac, "\x00\x11\x22\x33\x44\x55", 6); // Match Gateway MAC
        std::memcpy(hdr->src_mac, "\x12\x22\x33\x44\x55\x66", 6); // Valid Unicast Source MAC
        finalize_tcp_checksum(hdr);
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
        hdr->tcp_window = slabflux::core::endian::host_to_network16(65535);
        std::memcpy(hdr->dest_mac, "\x00\x11\x22\x33\x44\x55", 6); 
        std::memcpy(hdr->src_mac, "\x12\x22\x33\x44\x55\x66", 6);
        finalize_tcp_ipv6_checksum(hdr); 
    }

    void finalize_tcp_checksum(raw_tcp_ipv4_frame* hdr, uint32_t opt_len = 0) {
        uint16_t tcp_len = 20 + opt_len;
        uint32_t pseudo_sum = 0;
        uint32_t src = hdr->ip_src;
        uint32_t dst = hdr->ip_dst;
        pseudo_sum += (src & 0xFFFF) + (src >> 16);
        pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
        pseudo_sum += slabflux::core::endian::host_to_network16(6);
        pseudo_sum += slabflux::core::endian::host_to_network16(tcp_len);
        hdr->tcp_checksum = 0;
        hdr->tcp_checksum = slabflux::net::tcp_wire_engine::compute_checksum(&hdr->tcp_src_port, tcp_len, pseudo_sum);
    }

    void finalize_tcp_ipv6_checksum(raw_tcp_ipv6_frame* hdr, uint32_t opt_len = 0) {
        uint16_t tcp_len = 20 + opt_len;
        uint32_t pseudo_sum = 0;
        const uint16_t* src16 = reinterpret_cast<const uint16_t*>(hdr->ipv6_src);
        const uint16_t* dst16 = reinterpret_cast<const uint16_t*>(hdr->ipv6_dst);
        for(int i=0; i<8; ++i) pseudo_sum += src16[i];
        for(int i=0; i<8; ++i) pseudo_sum += dst16[i];
        pseudo_sum += slabflux::core::endian::host_to_network16(6);
        pseudo_sum += slabflux::core::endian::host_to_network16(tcp_len);
        hdr->tcp_checksum = 0;
        hdr->tcp_checksum = slabflux::net::tcp_wire_engine::compute_checksum(&hdr->tcp_src_port, tcp_len, pseudo_sum);
    }

    // Helper to initialize a perfectly valid L2/L3/L4 baseline UDP frame
    void init_valid_udp_base(raw_udp_ipv4_frame* hdr) {
        std::memset(hdr, 0, sizeof(raw_udp_ipv4_frame));
        hdr->eth_type = slabflux::core::endian::host_to_network16(0x0800);
        hdr->ip_ihl_ver = 0x45;
        hdr->ip_len = slabflux::core::endian::host_to_network16(28); // 20 + 8
        hdr->ip_ttl = 64;
        hdr->ip_protocol = 17;
        hdr->ip_src = slabflux::core::endian::host_to_network32(0x08080808);
        hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A000001);
        hdr->ip_checksum = slabflux::net::tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
        hdr->udp_src_port = slabflux::core::endian::host_to_network16(12345);
        hdr->udp_dst_port = slabflux::core::endian::host_to_network16(53);
        hdr->udp_length = slabflux::core::endian::host_to_network16(8);
        hdr->dest_mac[0] = 0x00;
        std::memcpy(hdr->dest_mac, "\x00\x11\x22\x33\x44\x55", 6); // Match Gateway MAC
        std::memcpy(hdr->src_mac, "\x12\x22\x33\x44\x55\x66", 6); // Valid Unicast Source MAC
    }

    void init_valid_udp_ipv6_base(raw_udp_ipv6_frame* hdr) {
        std::memset(hdr, 0, sizeof(raw_udp_ipv6_frame));
        hdr->eth_type = slabflux::core::endian::host_to_network16(0x86DD);
        hdr->ipv6_flow = slabflux::core::endian::host_to_network32(0x60000000);
        hdr->ipv6_plen = slabflux::core::endian::host_to_network16(8); 
        hdr->ipv6_nxt = 17;
        hdr->ipv6_hlim = 64;
        hdr->ipv6_src[0] = slabflux::core::endian::host_to_network64(0x20010db800000000ULL); 
        hdr->ipv6_src[1] = slabflux::core::endian::host_to_network64(0x0000000000000002ULL); 
        hdr->ipv6_dst[0] = slabflux::core::endian::host_to_network64(0xfe80000000000000ULL); 
        hdr->ipv6_dst[1] = slabflux::core::endian::host_to_network64(0x0000000000000001ULL);
        hdr->udp_src_port = slabflux::core::endian::host_to_network16(12345);
        hdr->udp_dst_port = slabflux::core::endian::host_to_network16(53);
        hdr->udp_length = slabflux::core::endian::host_to_network16(8);
        std::memcpy(hdr->dest_mac, "\x00\x11\x22\x33\x44\x55", 6);
        std::memcpy(hdr->src_mac, "\x12\x22\x33\x44\x55\x66", 6);
    }

    void inject_frame(void* buffer, uint32_t len, uint64_t timestamp) {
        struct rte_mempool* pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
        struct rte_mbuf* mbuf = rte_pktmbuf_alloc(pool);
        uint32_t physical_len = len < 64 ? 64 : len; // Pad to Ethernet minimum length
        if (mbuf) {
            char* mbuf_data = rte_pktmbuf_mtod(mbuf, char*);
            std::memset(mbuf_data, 0, physical_len);
            std::memcpy(mbuf_data, buffer, len);
            mbuf->data_len = physical_len;
            mbuf->pkt_len = physical_len;
            gateway.on_raw_frame(mbuf_data, physical_len, timestamp, mbuf);
        } else {
            gateway.on_raw_frame(static_cast<const char*>(buffer), physical_len, timestamp);
        }
    }
};

// ============================================================================
// GEOMETRIC BOUNDARY & TOPOLOGICAL EXCLUSION TESTS
// ============================================================================

TEST_F(PublicGatewaySecurityTest, DropsMartianPackets) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    // Inject 127.0.0.1 as source (Martian Loopback Spoofing / CVE-1999-0186)
    hdr->ip_src = slabflux::core::endian::host_to_network32(0x7F000001); 
    hdr->ip_checksum = 0;
    hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
    finalize_tcp_checksum(hdr);

    inject_frame(buffer, 54, 1000);
    EXPECT_EQ(egress_ring.available_to_peek(), 0); // No SYN-ACK response
}

TEST_F(PublicGatewaySecurityTest, DropsLandAttack) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    // Inject src == dst (LAND Attack / CVE-1999-0115 Infinite Intersection)
    hdr->ip_src = hdr->ip_dst; 
    hdr->ip_checksum = 0;
    hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
    finalize_tcp_checksum(hdr);

    inject_frame(buffer, 54, 1000);
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsBroadcastMacSynReflection) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    // Inject Broadcast MAC into source, projecting a reflection storm risk
    hdr->src_mac[0] = 0xFF; 

    inject_frame(buffer, 54, 1000);
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsIpv6MulticastSource) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv6_frame*>(buffer);
    init_valid_tcp_ipv6_base(hdr);
    
    hdr->ipv6_src[0] = slabflux::core::endian::host_to_network64(0xFF02000000000000ULL); // Multicast starting with FF
    finalize_tcp_ipv6_checksum(hdr);

    inject_frame(buffer, 74, 1000);
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsIpv6ExtensionHeaderHijack) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv6_frame*>(buffer);
    init_valid_tcp_ipv6_base(hdr);
    
    // Inject a Hop-by-Hop extension header
    hdr->ipv6_nxt = 0; // Next Header is Hop-by-Hop
    
    // Craft the extension header after the main IPv6 header (at offset 54)
    char* ext_hdr_ptr = buffer + 54;
    ext_hdr_ptr[0] = 6; // Next Header is TCP
    ext_hdr_ptr[1] = 0; // Header Extension Length (0 = 8 bytes)
    
    hdr->ipv6_plen = slabflux::core::endian::host_to_network16(20 + 8); // TCP len + Ext Hdr len

    inject_frame(buffer, 74 + 8, 1000);
    EXPECT_EQ(egress_ring.available_to_peek(), 0); // Must be dropped
}

TEST_F(PublicGatewaySecurityTest, DropsSynWithPayload) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    // Add 10 bytes of payload to SYN (ChinaZ Botnet Defense)
    hdr->ip_len = slabflux::core::endian::host_to_network16(50);
    hdr->ip_checksum = 0;
    hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
    finalize_tcp_checksum(hdr, 10);

    inject_frame(buffer, 64, 1000); // 14 + 50
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsIpv6SynWithPayload) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv6_frame*>(buffer);
    init_valid_tcp_ipv6_base(hdr);
    
    hdr->ipv6_plen = slabflux::core::endian::host_to_network16(30); // 20 TCP + 10 payload
    finalize_tcp_ipv6_checksum(hdr, 10);

    inject_frame(buffer, 84, 1000); 
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsInvalidTcpFlags) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    // TCP Flag Anomaly (XMAS / SYN-FIN scans)
    hdr->tcp_flags = FLAG_SYN | FLAG_FIN; 
    finalize_tcp_checksum(hdr);

    inject_frame(buffer, 54, 1000);
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsL3ChecksumCorrupted) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    // Corrupt the hardware IP Checksum
    hdr->ip_checksum = 0xDEAD; 

    inject_frame(buffer, 54, 1000);
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsL4TcpChecksumCorrupted) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    // Corrupt the hardware TCP Checksum
    hdr->tcp_checksum = 0xBEEF; 

    inject_frame(buffer, 54, 1000);
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsIpOptionsHijack) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    // Manipulate IP header length to force L4 parsing offsets out of bounds
    hdr->ip_ihl_ver = 0x46; // IHL = 6 (24 bytes)
    hdr->ip_len = slabflux::core::endian::host_to_network16(44); // 24 + 20
    hdr->ip_checksum = 0;
    hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 24, 0);

    inject_frame(buffer, 58, 1000);
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsMulticastSource) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    // Source IP within Class D Multicast range (224.0.0.1)
    hdr->ip_src = slabflux::core::endian::host_to_network32(0xE0000001);
    hdr->ip_checksum = 0;
    hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);

    inject_frame(buffer, 54, 1000);
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsPromiscuousMacMismatch) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    // Unicast MAC, but NOT our gateway's MAC (Tests Promiscuous Mode Side-Channel Drops)
    hdr->dest_mac[5] = 0x99; 

    inject_frame(buffer, 54, 1000);
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsZeroPortMultiplexing) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    // Source Port 0 is illegal in TCP and heavily used for stealth scanning
    hdr->tcp_src_port = 0; 

    inject_frame(buffer, 54, 1000);
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsEthernetPaddingOobRead) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    // Logical IP length maliciously claims less than the TCP+IP header size
    // Tests against IP Length Underflow bounding operators
    hdr->ip_len = slabflux::core::endian::host_to_network16(30); 
    hdr->ip_checksum = 0;
    hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);

    inject_frame(buffer, 64, 1000); // Pass minimum ethernet physical size
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

// ============================================================================
// UDP BOUNDARY & ICMP ORACLE TESTS
// ============================================================================

TEST_F(PublicGatewaySecurityTest, VaporizesUdpPortZero) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_udp_ipv4_frame*>(buffer);
    init_valid_udp_base(hdr);
    hdr->udp_dst_port = 0; // Invalid destination
    
    inject_frame(buffer, 42, 1000);
    EXPECT_EQ(defrag.udp_datagram_calls, 0);
}

TEST_F(PublicGatewaySecurityTest, VaporizesIpv6UdpPortZero) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_udp_ipv6_frame*>(buffer);
    init_valid_udp_ipv6_base(hdr);
    hdr->udp_dst_port = 0; 
    
    inject_frame(buffer, 62, 1000);
    EXPECT_EQ(defrag.udp_datagram_calls, 0);
}

TEST_F(PublicGatewaySecurityTest, VaporizesTruncatedUdp) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_udp_ipv4_frame*>(buffer);
    std::memset(hdr, 0, sizeof(raw_udp_ipv4_frame));
    hdr->eth_type = slabflux::core::endian::host_to_network16(0x0800);
    hdr->ip_ihl_ver = 0x45;
    hdr->ip_len = slabflux::core::endian::host_to_network16(120); // Claims 120 bytes
    hdr->ip_protocol = 17; // UDP
    hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
    hdr->udp_length = slabflux::core::endian::host_to_network16(100);
    
    // Pass only 42 physical bytes (Truncated physical length vs logical)
    inject_frame(buffer, 42, 1000);
    
    // Must be completely vaporized (no ICMP reflection oracle, no defrag dispatch)
    EXPECT_EQ(defrag.udp_datagram_calls, 0);
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, VaporizesUdpLogicalUnderflow) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_udp_ipv4_frame*>(buffer);
    init_valid_udp_base(hdr);
    
    // Logical IP length is mathematically smaller than the L3+L4 headers
    hdr->ip_len = slabflux::core::endian::host_to_network16(25);
    hdr->ip_checksum = 0;
    hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
    
    inject_frame(buffer, 64, 1000); // Full physical buffer
    EXPECT_EQ(defrag.udp_datagram_calls, 0);
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsIcmpSmurfAmplification) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    // Re-purpose as a generic UDP packet targeting a broadcast IP (10.0.0.255)
    hdr->ip_protocol = 17;
    hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A0000FF); 
    hdr->ip_checksum = 0;
    hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);

    inject_frame(buffer, 42, 1000);
    // Must be silently dropped; no ICMP Port Unreachable reflection Oracle!
    EXPECT_EQ(egress_ring.available_to_peek(), 0);
}

TEST_F(PublicGatewaySecurityTest, DropsIcmpMongolianPingOfDeath) {
    alignas(2048) char buffer[2048]; // Giant Frame
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    hdr->ip_protocol = 1; // ICMP
    buffer[34] = 8; // Echo Request

    inject_frame(buffer, 1600, 1000); // Exceeds Ethernet MTU
    EXPECT_EQ(egress_ring.available_to_peek(), 0); // No Echo Reply generated
}

// ============================================================================
// L2 ENCAPSULATION & ETHERLEAK TESTS
// ============================================================================

TEST_F(PublicGatewaySecurityTest, DropsArpReflectionStorm) {
    alignas(64) char buffer[128];
    std::memset(buffer, 0, 128);
    auto* arp = reinterpret_cast<raw_arp_frame*>(buffer);
    
    std::memcpy(arp->dest_mac, "\xFF\xFF\xFF\xFF\xFF\xFF", 6);
    std::memcpy(arp->src_mac, "\x01\x11\x22\x33\x44\x55", 6); // Multicast Source MAC!
    arp->eth_type = slabflux::core::endian::host_to_network16(0x0806);
    arp->hw_type = slabflux::core::endian::host_to_network16(1);
    arp->proto_type = slabflux::core::endian::host_to_network16(0x0800);
    arp->hw_len = 6;
    arp->proto_len = 4;
    arp->opcode = slabflux::core::endian::host_to_network16(1); // Request
    std::memcpy(arp->sender_mac, "\x01\x11\x22\x33\x44\x55", 6);
    arp->sender_ip = slabflux::core::endian::host_to_network32(0x08080808);
    arp->target_ip = slabflux::core::endian::host_to_network32(0x0A000001); // Gateway IP
    
    inject_frame(buffer, 64, 1000);
    EXPECT_EQ(egress_ring.available_to_peek(), 0); // No ARP Reply
}

TEST_F(PublicGatewaySecurityTest, PadsIcmpEtherleak) {
    alignas(64) char buffer[128];
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer);
    init_valid_tcp_base(hdr);
    
    hdr->ip_protocol = 1; // ICMP
    buffer[34] = 8; // Echo Request
    buffer[35] = 0; // Echo Code
    buffer[36] = 0; // Clear checksum
    buffer[37] = 0;
    *reinterpret_cast<uint16_t*>(&buffer[36]) = slabflux::net::tcp_wire_engine::compute_checksum(&buffer[34], 8, 0);

    hdr->ip_len = slabflux::core::endian::host_to_network16(28);
    hdr->ip_checksum = 0;
    hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
    
    inject_frame(buffer, 42, 1000);
    
    ASSERT_EQ(egress_ring.available_to_peek(), 1);
    auto* reply_mbuf = egress_ring.get_peek_slot(0)->mbuf;
    
    // DPDK mbuf must have explicitly padded the frame to 60 bytes to prevent Etherleak
    EXPECT_EQ(reply_mbuf->data_len, 60);
    EXPECT_EQ(reply_mbuf->pkt_len, 60);
    egress_ring.consume_n(1);
}

// ============================================================================
// SYN COOKIE & CONNECTION STATE ATTACK MITIGATION
// ============================================================================

TEST_F(PublicGatewaySecurityTest, DropsExpiredSynCookie) {
    alignas(64) char syn_buffer[128];
    auto* syn = reinterpret_cast<raw_tcp_ipv4_frame*>(syn_buffer);
    init_valid_tcp_base(syn);
    syn->tcp_seq = slabflux::core::endian::host_to_network32(2000);
    finalize_tcp_checksum(syn);
    
    inject_frame(syn_buffer, 54, 1000); // Initial SYN at T=1000ms
    
    ASSERT_EQ(egress_ring.available_to_peek(), 1);
    uint32_t cookie = slabflux::core::endian::network_to_host32(
        reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(egress_ring.get_peek_slot(0)->mbuf, void*))->tcp_seq
    );
    egress_ring.consume_n(1);
    
    alignas(64) char ack_buffer[128];
    auto* ack = reinterpret_cast<raw_tcp_ipv4_frame*>(ack_buffer);
    init_valid_tcp_base(ack);
    ack->tcp_flags = FLAG_ACK;
    ack->tcp_seq = slabflux::core::endian::host_to_network32(2001);
    ack->tcp_ack = slabflux::core::endian::host_to_network32(cookie + 1);
    finalize_tcp_checksum(ack);
    
    // Send ACK over 2 minutes later (T=150000ms). Temporal boundary should reject it.
    inject_frame(ack_buffer, 54, 150000);
    
    EXPECT_EQ(accept_ring.available_to_peek(), 0); // Dropped
}

TEST_F(PublicGatewaySecurityTest, MitigatesAcceptQueueGhostLeak) {
    // Dynamically saturate the accept queue to its absolute maximum usable capacity
    // regardless of internal cache-line padding or power-of-two rounding adjustments.
    // Capped safely at 4096 to prevent infinite loop deadlocks in un-bounded mocks.
    for (int i = 0; i < 4096; ++i) {
        uint32_t* slot = accept_ring.get_reserved_slot(0);
        if (!slot) break; // Ring is mathematically saturated
        *slot = i;
        accept_ring.commit_n(1);
    }
    
    // Now trigger a valid SYN Cookie rehydration
    alignas(64) char syn_buffer[128];
    auto* syn = reinterpret_cast<raw_tcp_ipv4_frame*>(syn_buffer);
    init_valid_tcp_base(syn);
    syn->tcp_seq = slabflux::core::endian::host_to_network32(3000);
    finalize_tcp_checksum(syn);
    
    inject_frame(syn_buffer, 54, 1000);
    ASSERT_EQ(egress_ring.available_to_peek(), 1);
    
    auto* peek_slot = egress_ring.get_peek_slot(0);
    uint32_t cookie = slabflux::core::endian::network_to_host32(
        reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(peek_slot->mbuf, void*))->tcp_seq
    );
    rte_pktmbuf_free(peek_slot->mbuf); // Free the mock memory!
    const_cast<slabflux::net::outbound_tcp_segment<1460>*>(peek_slot)->mbuf = nullptr;
    egress_ring.consume_n(1);
    
    alignas(64) char ack_buffer[128];
    auto* ack = reinterpret_cast<raw_tcp_ipv4_frame*>(ack_buffer);
    init_valid_tcp_base(ack);
    ack->tcp_flags = FLAG_ACK;
    ack->tcp_seq = slabflux::core::endian::host_to_network32(3001);
    ack->tcp_ack = slabflux::core::endian::host_to_network32(cookie + 1);
    finalize_tcp_checksum(ack);
    
    inject_frame(ack_buffer, 54, 1500);
    
    // Because the accept ring is full, the gateway MUST drop the connection
    // and generate an immediate RST to prevent a ghost connection leak.
    std::cerr << "[DEBUG] egress_ring count: " << egress_ring.available_to_peek() << std::endl;
    ASSERT_EQ(egress_ring.available_to_peek(), 1);
    auto* rst = reinterpret_cast<raw_tcp_ipv4_frame*>(
        rte_pktmbuf_mtod(egress_ring.get_peek_slot(0)->mbuf, void*)
    );
    EXPECT_EQ(rst->tcp_flags, FLAG_RST); // Peer is out of sync; must trigger explicit stateless RST
    egress_ring.consume_n(1);
}

// ============================================================================
// PUBLIC API & VIRTUAL SOCKET TESTS
// ============================================================================

TEST_F(PublicGatewaySecurityTest, VirtualSocketConnectTriggersArpHalt) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_CLOSED;
    tcb.tx_egress_conduit = reinterpret_cast<void*>(&egress_ring);
    tcb.tx_unacked_ring = reinterpret_cast<void*>(&egress_ring);
    tcb.tx_mbuf_pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    
    virtual_tcp_socket sock(&tcb);
    EXPECT_TRUE(sock.is_valid());
    EXPECT_FALSE(sock.is_established());
    
    // Connect should transition to SYN_SENT but halt waiting on ARP mapping
    bool result = sock.connect(0x08080808, 80);
    EXPECT_FALSE(result); // Fails due to ARP unresolved target_mac == 0
    EXPECT_EQ(tcb.phase_mask, PHASE_SYN_SENT);
    EXPECT_TRUE(tcb.temporal_flags & TEMP_FLAG_ARP_WAIT);
}

// ============================================================================
// IP DEFRAGMENTATION & REASSEMBLY TESTS (UDP)
// ============================================================================

TEST_F(PublicGatewaySecurityTest, EvictsSlowlorisFragments) {
    alignas(64) char frag1[128];
    alignas(64) char frag2[128];
    
    std::memset(frag1, 0, sizeof(frag1));
    std::memset(frag2, 0, sizeof(frag2));

    auto* hdr1 = reinterpret_cast<raw_tcp_ipv4_frame*>(frag1);
    auto* hdr2 = reinterpret_cast<raw_tcp_ipv4_frame*>(frag2);

    for (auto* hdr : {hdr1, hdr2}) {
        hdr->eth_type = slabflux::core::endian::host_to_network16(0x0800);
        hdr->ip_ihl_ver = 0x45;
        hdr->ip_ttl = 64;
        hdr->ip_protocol = 17;
        hdr->ip_src = slabflux::core::endian::host_to_network32(0x08080808);
        hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A000001);
        hdr->ip_id = slabflux::core::endian::host_to_network16(0x5555);
        std::memcpy(hdr->dest_mac, "\x00\x11\x22\x33\x44\x55", 6);
    }

    hdr1->ip_len = slabflux::core::endian::host_to_network16(36);
    hdr1->ip_frag_offset = slabflux::core::endian::host_to_network16(0x2000); 
    hdr1->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr1) + 14, 20, 0);

    hdr2->ip_len = slabflux::core::endian::host_to_network16(28);
    hdr2->ip_frag_offset = slabflux::core::endian::host_to_network16(2); 
    hdr2->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr2) + 14, 20, 0);

    inject_frame(frag1, 50, 1000);
    EXPECT_EQ(defrag.udp_datagram_calls, 0);

    // 35 seconds later (> 30000ms timeout)
    inject_frame(frag2, 42, 36000);
    
    // Slowloris timeout engaged: Frag 1 was evicted, so Frag 2 cannot complete the datagram.
    EXPECT_EQ(defrag.udp_datagram_calls, 0); 
}

TEST_F(PublicGatewaySecurityTest, SurvivesHashBucketSaturation) {
    for (int i = 0; i < 2048; ++i) {
        alignas(64) char frag[128];
        std::memset(frag, 0, sizeof(frag));
        auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(frag);
        hdr->eth_type = slabflux::core::endian::host_to_network16(0x0800);
        hdr->ip_ihl_ver = 0x45;
        hdr->ip_ttl = 64;
        hdr->ip_protocol = 17;
        hdr->ip_src = slabflux::core::endian::host_to_network32(0x08080808 + i);
        hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A000001);
        hdr->ip_id = slabflux::core::endian::host_to_network16(i);
        hdr->ip_len = slabflux::core::endian::host_to_network16(36);
        hdr->ip_frag_offset = slabflux::core::endian::host_to_network16(0x2000);
        hdr->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
        std::memcpy(hdr->dest_mac, "\x00\x11\x22\x33\x44\x55", 6);
        
        inject_frame(frag, 50, 1000);
    }
    // Must gracefully drop packets when the 8-way linear probe is saturated, 
    // never crashing or stalling.
    SUCCEED() << "Gateway survived 2048 distinct incomplete fragments without crashing.";
}

TEST_F(PublicGatewaySecurityTest, StripsVlanQInQTags) {
    alignas(64) char buffer[128];
    std::memset(buffer, 0, 128);
    
    // Inject two VLAN tags (0x88A8 and 0x8100)
    std::memcpy(buffer, "\x00\x11\x22\x33\x44\x55", 6); // Dest MAC
    std::memcpy(buffer + 6, "\x12\x22\x33\x44\x55\x66", 6); // Src MAC
    *reinterpret_cast<uint16_t*>(buffer + 12) = slabflux::core::endian::host_to_network16(0x88A8); // Outer VLAN
    *reinterpret_cast<uint16_t*>(buffer + 16) = slabflux::core::endian::host_to_network16(0x8100); // Inner VLAN
    
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(buffer + 8); // Shifted by 8 bytes
    hdr->eth_type = slabflux::core::endian::host_to_network16(0x0800);
    hdr->ip_ihl_ver = 0x45;
    hdr->ip_len = slabflux::core::endian::host_to_network16(40);
    hdr->ip_ttl = 64;
    hdr->ip_protocol = 6;
    hdr->ip_src = slabflux::core::endian::host_to_network32(0x08080808);
    hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A000001);
    hdr->ip_checksum = slabflux::net::tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
    hdr->tcp_src_port = slabflux::core::endian::host_to_network16(12345);
    hdr->tcp_dst_port = slabflux::core::endian::host_to_network16(80);
    hdr->tcp_data_offset = 0x50;
    hdr->tcp_flags = FLAG_SYN;
    finalize_tcp_checksum(hdr);
    
    // Total size = 14 (Eth) + 8 (VLAN) + 20 (IP) + 20 (TCP) = 62
    inject_frame(buffer, 62, 1000);
    
    // The gateway should strip both VLAN tags and process the SYN, generating a SYN-ACK
    EXPECT_EQ(egress_ring.available_to_peek(), 1);
    egress_ring.consume_n(1);
}

// ============================================================================
// SYN COOKIE & CONNECTION STATE ATTACK MITIGATION
// ============================================================================

TEST_F(PublicGatewaySecurityTest, SynthesizesSynCookieAndRehydrates) {
    alignas(64) char syn_buffer[128];
    auto* syn = reinterpret_cast<raw_tcp_ipv4_frame*>(syn_buffer);
    init_valid_tcp_base(syn);
    syn->tcp_seq = slabflux::core::endian::host_to_network32(4000);
    finalize_tcp_checksum(syn);
    
    inject_frame(syn_buffer, 54, 1000); // T=1000ms
    
    ASSERT_EQ(egress_ring.available_to_peek(), 1);
    auto* syn_ack = reinterpret_cast<raw_tcp_ipv4_frame*>(
        rte_pktmbuf_mtod(egress_ring.get_peek_slot(0)->mbuf, void*)
    );
    
    EXPECT_EQ(syn_ack->tcp_flags, FLAG_SYN | FLAG_ACK);
    uint32_t cookie = slabflux::core::endian::network_to_host32(syn_ack->tcp_seq);
    egress_ring.consume_n(1);
    
    // Craft ACK to complete handshake
    alignas(64) char ack_buffer[128];
    auto* ack = reinterpret_cast<raw_tcp_ipv4_frame*>(ack_buffer);
    init_valid_tcp_base(ack);
    ack->tcp_flags = FLAG_ACK;
    ack->tcp_seq = slabflux::core::endian::host_to_network32(4001); // Client ISN + 1
    ack->tcp_ack = slabflux::core::endian::host_to_network32(cookie + 1);
    finalize_tcp_checksum(ack);
    
    // Send ACK slightly later (T=1500ms)
    inject_frame(ack_buffer, 54, 1500);
    
    // Connection must be established and routed to accept ring
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    uint32_t conn_id = *accept_ring.get_peek_slot(0);
    accept_ring.consume_n(1);
    
    auto& tcb = gateway.get_tcb(conn_id);
    EXPECT_EQ(tcb.phase_mask, PHASE_ESTABLISHED);
}

// ============================================================================
// PUBLIC GATEWAY RFC & FIB TESTS
// ============================================================================

TEST(PublicTcpGatewayTest, RespondsToIcmpEcho) {
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    MockDefragmenter defrag;
    auto pub_gateway = std::make_unique<public_tcp_gateway<MockDefragmenter, 1024>>(defrag);
    
    struct rte_mempool* pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    
    pub_gateway->bind_conduits(&tx_ring, nullptr);
    pub_gateway->bind_mempool(pool);
    pub_gateway->set_local_identity(
        slabflux::core::endian::host_to_network32(0x0A000002), 
        (const uint8_t*)"\xAA\xBB\xCC\xDD\xEE\xFF"
    );

    alignas(64) char raw_buffer[64];
    std::memset(raw_buffer, 0, 64);
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(raw_buffer);
    std::memcpy(hdr->dest_mac, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    std::memcpy(hdr->src_mac, "\x12\x22\x33\x44\x55\x66", 6);
    *reinterpret_cast<uint16_t*>(&raw_buffer[12]) = slabflux::core::endian::host_to_network16(0x0800);
    hdr->ip_ihl_ver = 0x45;
    hdr->ip_len = slabflux::core::endian::host_to_network16(28); // 20 IP + 8 ICMP
    hdr->ip_protocol = 1; // ICMP
    hdr->ip_src = slabflux::core::endian::host_to_network32(0x0A000005);
    hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A000002);

    hdr->ip_checksum = 0;
    hdr->ip_checksum = slabflux::net::tcp_wire_engine::compute_checksum(&hdr->ip_ihl_ver, 20, 0);
    
    raw_buffer[34] = 8; // ICMP Echo Request
    raw_buffer[35] = 0;
    *reinterpret_cast<uint16_t*>(&raw_buffer[36]) = 0;
    *reinterpret_cast<uint16_t*>(&raw_buffer[36]) = slabflux::net::tcp_wire_engine::compute_checksum(&raw_buffer[34], 8, 0);

    struct rte_mbuf* mbuf = rte_pktmbuf_alloc(pool);
    char* mbuf_data = rte_pktmbuf_mtod(mbuf, char*);
    std::memset(mbuf_data, 0, 64);
    std::memcpy(mbuf_data, raw_buffer, 42);
    mbuf->data_len = 64; // Supply mandatory hardware descriptors 
    mbuf->pkt_len = 64;
    
    pub_gateway->on_raw_frame(mbuf_data, 64, 1000, mbuf);

    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* reply = reinterpret_cast<char*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*));
    EXPECT_EQ(reply[34], 0); // ICMP Echo Reply Native Injection
}

TEST(PublicTcpGatewayTest, GeneratesRstForUnboundPorts) {
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    MockDefragmenter defrag;
    auto pub_gateway = std::make_unique<public_tcp_gateway<MockDefragmenter, 1024>>(defrag);
    
    struct rte_mempool* pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    
    pub_gateway->bind_conduits(&tx_ring, nullptr);
    pub_gateway->bind_mempool(pool);
    pub_gateway->set_local_identity(
        slabflux::core::endian::host_to_network32(0x0A000002), 
        (const uint8_t*)"\xAA\xBB\xCC\xDD\xEE\xFF"
    );

    alignas(64) char raw_buffer[64];
    std::memset(raw_buffer, 0, 64);
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(raw_buffer);
    std::memcpy(hdr->dest_mac, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    std::memcpy(hdr->src_mac, "\x12\x22\x33\x44\x55\x66", 6);
    *reinterpret_cast<uint16_t*>(&raw_buffer[12]) = slabflux::core::endian::host_to_network16(0x0800);
    hdr->ip_ihl_ver = 0x45;
    hdr->ip_len = slabflux::core::endian::host_to_network16(40);
    hdr->ip_protocol = 6;
    hdr->ip_src = slabflux::core::endian::host_to_network32(0x08080808);
    hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A000002);
    hdr->tcp_src_port = slabflux::core::endian::host_to_network16(12345);
    hdr->tcp_dst_port = slabflux::core::endian::host_to_network16(80);
    hdr->tcp_flags = FLAG_ACK; // Malicious or stale inbound ACK to closed port
    hdr->tcp_seq = slabflux::core::endian::host_to_network32(1000);
    hdr->tcp_ack = slabflux::core::endian::host_to_network32(5000);
    hdr->tcp_data_offset = 0x50;
    
    hdr->ip_checksum = 0;
    hdr->ip_checksum = slabflux::net::tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);

    uint32_t pseudo_sum = 0;
    uint32_t src = hdr->ip_src;
    uint32_t dst = hdr->ip_dst;
    pseudo_sum += (src & 0xFFFF) + (src >> 16);
    pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
    pseudo_sum += slabflux::core::endian::host_to_network16(6);
    pseudo_sum += slabflux::core::endian::host_to_network16(20);
    hdr->tcp_checksum = 0;
    hdr->tcp_checksum = slabflux::net::tcp_wire_engine::compute_checksum(&hdr->tcp_src_port, 20, pseudo_sum);

    struct rte_mbuf* mbuf = rte_pktmbuf_alloc(pool);
    char* mbuf_data = rte_pktmbuf_mtod(mbuf, char*);
    std::memset(mbuf_data, 0, 64);
    std::memcpy(mbuf_data, raw_buffer, 54);
    mbuf->data_len = 64; // Supply mandatory hardware descriptors 
    mbuf->pkt_len = 64;
    
    pub_gateway->on_raw_frame(mbuf_data, 64, 1000, mbuf);

    // The Gateway must natively protect the system by generating an immediate TCP Reset
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* reply = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*));
    EXPECT_EQ(reply->tcp_flags, FLAG_RST);
    EXPECT_EQ(reply->tcp_seq, slabflux::core::endian::host_to_network32(5000));
}

TEST(PublicTcpGatewayTest, FIB_RoutesToDefaultGateway_AndEphemeralPort) {
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    MockDefragmenter defrag;
    auto pub_gateway = std::make_unique<public_tcp_gateway<MockDefragmenter, 1024>>(defrag);
    
    struct rte_mempool* pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    
    pub_gateway->bind_conduits(&tx_ring, nullptr);
    pub_gateway->bind_mempool(pool);
    pub_gateway->set_local_identity(
        slabflux::core::endian::host_to_network32(0x0A000002), 
        (const uint8_t*)"\xAA\xBB\xCC\xDD\xEE\xFF",
        slabflux::core::endian::host_to_network32(0xFFFFFF00),
        slabflux::core::endian::host_to_network32(0x0A000001) // Default GW: 10.0.0.1
    );

    // Use the native Active Open allocator to establish out-of-band traffic
    uint32_t conn_id = pub_gateway->connect_outbound(
        slabflux::core::endian::host_to_network32(0x08080808), 80
    );
    ASSERT_NE(conn_id, 0);

    tcp_transmission_control_block& tcb = pub_gateway->get_tcb(conn_id);
    
    // Verify ephemeral port assigned falls within the IANA standard 49152 - 65535 range
    uint16_t local_port = slabflux::core::endian::network_to_host16(tcb.local_port);
    EXPECT_GE(local_port, 49152);
    EXPECT_LE(local_port, 65535);

    pub_gateway->poll_temporal(1000);

    // We should expect an ARP request natively routed to the DEFAULT GATEWAY, NOT 8.8.8.8
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    auto* arp_req = reinterpret_cast<raw_arp_frame*>(rte_pktmbuf_mtod(tx_ring.get_peek_slot(0)->mbuf, void*));
    EXPECT_EQ(arp_req->target_ip, slabflux::core::endian::host_to_network32(0x0A000001)); 
}

// ============================================================================
// FULL UDP SUPPORT INTEGRATION TESTS
// ============================================================================

TEST(PublicTcpGatewayTest, InboundUdpRouting) {
    MockDefragmenter defrag;
    auto pub_gateway = std::make_unique<public_tcp_gateway<MockDefragmenter, 1024>>(defrag);
    
    struct rte_mempool* pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    pub_gateway->bind_mempool(pool);
    pub_gateway->set_local_identity(
        slabflux::core::endian::host_to_network32(0x0A000002), 
        (const uint8_t*)"\xAA\xBB\xCC\xDD\xEE\xFF"
    );

    alignas(64) char raw_buffer[64];
    std::memset(raw_buffer, 0, 64);
    auto* hdr = reinterpret_cast<raw_udp_ipv4_frame*>(raw_buffer);
    std::memcpy(hdr->dest_mac, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
    std::memcpy(hdr->src_mac, "\x12\x22\x33\x44\x55\x66", 6);
    hdr->eth_type = slabflux::core::endian::host_to_network16(0x0800);
    hdr->ip_ihl_ver = 0x45;
    hdr->ip_len = slabflux::core::endian::host_to_network16(33); // 20 IP + 8 UDP + 5 Payload
    hdr->ip_protocol = 17; // UDP
    hdr->ip_src = slabflux::core::endian::host_to_network32(0x08080808);
    hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A000002);
    hdr->ip_checksum = 0;
    hdr->ip_checksum = slabflux::net::tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
    
    hdr->udp_src_port = slabflux::core::endian::host_to_network16(12345);
    hdr->udp_dst_port = slabflux::core::endian::host_to_network16(53);
    hdr->udp_length = slabflux::core::endian::host_to_network16(13); // 8 + 5
    hdr->udp_checksum = 0; // Checksum 0 is mathematically valid under RFC 768
    
    std::memcpy(raw_buffer + 42, "hello", 5);
    
    pub_gateway->on_raw_frame(raw_buffer, 64, 1000, nullptr);
    
    // Gateway must natively bypass the ICMP Port Unreachable generator and call on_udp()
    EXPECT_EQ(defrag.udp_datagram_calls, 1);
}

TEST(TcpGatewayTest, VirtualUdpSocketSend) {
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    auto defrag = std::make_unique<MockDefragmenter>();
    auto gateway = std::make_unique<public_tcp_gateway<MockDefragmenter, 1024>>(*defrag); 
    
    struct rte_mempool* pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    gateway->bind_conduits(&tx_ring, nullptr);
    gateway->bind_mempool(pool);
    uint8_t gw_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
    gateway->set_local_identity(slabflux::core::endian::host_to_network32(0x0A000001), gw_mac, 0);

    virtual_udp_socket<public_tcp_gateway<MockDefragmenter, 1024>> udp_sock(*gateway, 12345);
    
    // Application Layer fires a stateless packet without allocating memory
    bool sent = udp_sock.send_to(slabflux::core::endian::host_to_network32(0x0A000005), 
                                 53, 12345, "query", 5);
                                 
    // Should return false because we need ARP resolution
    EXPECT_FALSE(sent);
    ASSERT_EQ(tx_ring.available_to_peek(), 1);
    
    // Validate geometric wire alignment of the ARP request
    auto* pkt = tx_ring.get_peek_slot(0);
    ASSERT_NE(pkt, nullptr);
    ASSERT_NE(pkt->mbuf, nullptr);
    auto* arp_hdr = reinterpret_cast<raw_arp_frame*>(rte_pktmbuf_mtod(pkt->mbuf, char*));
    
    EXPECT_EQ(arp_hdr->eth_type, slabflux::core::endian::host_to_network16(0x0806));
    EXPECT_EQ(arp_hdr->target_ip, slabflux::core::endian::host_to_network32(0x0A000005));

    rte_pktmbuf_free(pkt->mbuf);
    const_cast<slabflux::net::outbound_tcp_segment<1460>*>(pkt)->mbuf = nullptr;
    tx_ring.consume_n(1);
}

// ============================================================================
// TCP STATE MACHINE & CVE MITIGATION TESTS
// ============================================================================

TEST_F(PublicGatewaySecurityTest, MitigatesCVE_2019_11478_SackSlothCpuExhaustion) {
    uint32_t conn_id = gateway.connect_outbound(
        slabflux::core::endian::host_to_network32(0x08080808), 80
    );
    auto& tcb = gateway.get_tcb(conn_id);
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.sack_permitted = 1;
    tcb.snd_una = 1000;
    tcb.snd_nxt = 5000; // 4000 bytes in flight
    tcb.rcv_nxt = 5000;
    
    alignas(64) char ack_buffer[128];
    auto* ack = reinterpret_cast<raw_tcp_ipv4_frame*>(ack_buffer);
    init_valid_tcp_base(ack);
    ack->tcp_flags = FLAG_ACK;
    ack->tcp_seq = slabflux::core::endian::host_to_network32(5000);
    ack->tcp_ack = slabflux::core::endian::host_to_network32(1000); 
    ack->tcp_src_port = tcb.remote_port;
    ack->tcp_dst_port = tcb.local_port;
    
    ack->tcp_data_offset = 0x80; // 32 bytes (20 + 12 opt)
    uint8_t* opt = reinterpret_cast<uint8_t*>(ack_buffer) + 54;
    opt[0] = 0x01; opt[1] = 0x01;
    opt[2] = 0x05; opt[3] = 0x0A; // SACK Block (10 bytes)
    
    // Inject a malicious SACK block where Left Edge >= snd_nxt (Acknowledging unsent data)
    *reinterpret_cast<uint32_t*>(&opt[4]) = slabflux::core::endian::host_to_network32(9000); // Left Edge (Beyond flight)
    *reinterpret_cast<uint32_t*>(&opt[8]) = slabflux::core::endian::host_to_network32(9500); // Right Edge
    
    ack->ip_len = slabflux::core::endian::host_to_network16(52);
    ack->ip_checksum = 0;
    ack->ip_checksum = slabflux::net::tcp_wire_engine::compute_checksum(&ack->ip_ihl_ver, 20, 0);
    
    uint32_t pseudo_sum = 0;
    uint32_t src = ack->ip_src;
    uint32_t dst = ack->ip_dst;
    pseudo_sum += (src & 0xFFFF) + (src >> 16);
    pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
    pseudo_sum += slabflux::core::endian::host_to_network16(6);
    pseudo_sum += slabflux::core::endian::host_to_network16(32);
    ack->tcp_checksum = 0;
    ack->tcp_checksum = slabflux::net::tcp_wire_engine::compute_checksum(&ack->tcp_src_port, 32, pseudo_sum);
    
    inject_frame(ack_buffer, 66, 2000);
    
    // Unsent data SACK MUST be silently dropped during SACK processing.
    // Ensure processing completes without memory faults.
    SUCCEED() << "Gateway natively absorbed the malicious SACK Sloth attack without looping or faulting.";
}

// ============================================================================
// TCP STATE MACHINE & CVE MITIGATION TESTS
// ============================================================================

TEST_F(PublicGatewaySecurityTest, MitigatesCVE_1999_0162_UnacknowledgedDataSpoofing) {
    uint32_t conn_id = gateway.connect_outbound(
        slabflux::core::endian::host_to_network32(0x08080808), 80
    );
    auto& tcb = gateway.get_tcb(conn_id);
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.snd_una = 1000;
    tcb.snd_nxt = 2000; // 1000 bytes in flight
    tcb.rcv_nxt = 5000;
    tcb.challenge_ack_cnt = 0;
    
    alignas(64) char ack_buffer[128];
    auto* ack = reinterpret_cast<raw_tcp_ipv4_frame*>(ack_buffer);
    init_valid_tcp_base(ack);
    ack->tcp_flags = FLAG_ACK;
    ack->tcp_seq = slabflux::core::endian::host_to_network32(5000);
    ack->tcp_ack = slabflux::core::endian::host_to_network32(3000); // Acknowledges 1000 bytes NEVER SENT!
    ack->tcp_src_port = tcb.remote_port;
    ack->tcp_dst_port = tcb.local_port;
    ack->ip_len = slabflux::core::endian::host_to_network16(40);
    ack->ip_checksum = 0;
    ack->ip_checksum = slabflux::net::tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(ack) + 14, 20, 0);
    finalize_tcp_checksum(ack);
    
    inject_frame(ack_buffer, 54, 2000);
    
    // Connection must recognize the spoofed ACK and trigger a Challenge ACK
    EXPECT_EQ(tcb.snd_una, 1000); // UNA must not advance
    ASSERT_EQ(egress_ring.available_to_peek(), 1);
    auto* reply = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(egress_ring.get_peek_slot(0)->mbuf, void*));
    EXPECT_EQ(reply->tcp_flags, FLAG_ACK);
    egress_ring.consume_n(1);
    EXPECT_EQ(tcb.challenge_ack_cnt, 1);
}

TEST_F(PublicGatewaySecurityTest, MitigatesCVE_2005_0068_BlindPmtudSpoofing) {
    uint32_t conn_id = gateway.connect_outbound(
        slabflux::core::endian::host_to_network32(0x08080808), 80
    );
    auto& tcb = gateway.get_tcb(conn_id);
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.remote_mss = 1460;
    tcb.snd_una = 1000;
    tcb.snd_nxt = 5000; // 4000 bytes in flight
    
    alignas(64) char icmp_buffer[128];
    std::memset(icmp_buffer, 0, sizeof(icmp_buffer));
    auto* hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(icmp_buffer);
    init_valid_tcp_base(hdr);
    
    // Outer ICMP (Dest Unreachable / Frag Needed)
    hdr->ip_protocol = 1; // ICMP
    hdr->ip_len = slabflux::core::endian::host_to_network16(56);
    hdr->ip_checksum = 0;
    hdr->ip_checksum = slabflux::net::tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr) + 14, 20, 0);
    icmp_buffer[34] = 3; // Dest Unreachable
    icmp_buffer[35] = 4; // Frag Needed
    *reinterpret_cast<uint16_t*>(&icmp_buffer[40]) = slabflux::core::endian::host_to_network16(500); // Next-Hop MTU
    
    // Inner IP Header (Returned payload)
    auto* inner_ip = reinterpret_cast<raw_tcp_ipv4_frame*>(&icmp_buffer[42 - 14]);
    inner_ip->ip_ihl_ver = 0x45;
    inner_ip->ip_protocol = 6;
    inner_ip->ip_src = tcb.local_ipv4;
    inner_ip->ip_dst = tcb.remote_ipv4;
    
    // Inject MALICIOUS Out-of-Window sequence number for the inner TCP header
    uint16_t* inner_ports = reinterpret_cast<uint16_t*>(&icmp_buffer[62]);
    inner_ports[0] = tcb.local_port;
    inner_ports[1] = tcb.remote_port;
    *reinterpret_cast<uint32_t*>(&icmp_buffer[66]) = slabflux::core::endian::host_to_network32(9999); // Way out of flight

    inner_ip->ip_len = slabflux::core::endian::host_to_network16(40);
    inner_ip->ip_checksum = 0;
    inner_ip->ip_checksum = slabflux::net::tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(inner_ip) + 14, 20, 0);
    
    *reinterpret_cast<uint16_t*>(&icmp_buffer[36]) = 0;
    *reinterpret_cast<uint16_t*>(&icmp_buffer[36]) = slabflux::net::tcp_wire_engine::compute_checksum(&icmp_buffer[34], 36, 0);
    
    inject_frame(icmp_buffer, 70, 1000);
    
    // Gateway MUST reject the PMTUD update because the inner sequence is spoofed
    EXPECT_EQ(tcb.remote_mss, 1460); // Unchanged!
    
    // Now inject a VALID in-flight sequence
    *reinterpret_cast<uint32_t*>(&icmp_buffer[66]) = slabflux::core::endian::host_to_network32(2000); // Valid
    *reinterpret_cast<uint16_t*>(&icmp_buffer[36]) = 0;
    *reinterpret_cast<uint16_t*>(&icmp_buffer[36]) = slabflux::net::tcp_wire_engine::compute_checksum(&icmp_buffer[34], 36, 0);
    inject_frame(icmp_buffer, 70, 1000);
    
    // Gateway MUST accept the PMTUD update
    EXPECT_EQ(tcb.remote_mss, 460); // 500 - 40
}

TEST_F(PublicGatewaySecurityTest, MitigatesCVE_2005_0068_BlindPmtudSpoofing_Ipv6) {
    uint64_t target_ipv6[2] = { slabflux::core::endian::host_to_network64(0x20010db800000000ULL), slabflux::core::endian::host_to_network64(0x0000000000000002ULL) };
    uint32_t conn_id = gateway.connect_outbound_ipv6(target_ipv6, 80);
    auto& tcb = gateway.get_tcb(conn_id);
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.remote_mss = 1220;
    tcb.snd_una = 1000;
    tcb.snd_nxt = 5000;
    
    alignas(64) char icmp_buffer[128];
    std::memset(icmp_buffer, 0, sizeof(icmp_buffer));
    auto* hdr = reinterpret_cast<raw_tcp_ipv6_frame*>(icmp_buffer);
    init_valid_tcp_ipv6_base(hdr);
    
    hdr->ipv6_nxt = 58; // ICMPv6
    hdr->ipv6_plen = slabflux::core::endian::host_to_network16(56);
    icmp_buffer[54] = 2; // Packet Too Big
    icmp_buffer[55] = 0; // Code
    *reinterpret_cast<uint32_t*>(&icmp_buffer[58]) = slabflux::core::endian::host_to_network32(1100); // MTU
    
    // Inner IPv6 (Returned payload) starts at offset 54 + 8 = 62
    auto* inner_ip = reinterpret_cast<raw_tcp_ipv6_frame*>(&icmp_buffer[62 - 14]); // Offset for struct layout
    inner_ip->ipv6_nxt = 6;
    inner_ip->ipv6_src[0] = tcb.local_ipv6[0]; inner_ip->ipv6_src[1] = tcb.local_ipv6[1];
    inner_ip->ipv6_dst[0] = tcb.remote_ipv6[0]; inner_ip->ipv6_dst[1] = tcb.remote_ipv6[1];
    
    uint16_t* inner_ports = reinterpret_cast<uint16_t*>(&icmp_buffer[102]);
    inner_ports[0] = tcb.local_port;
    inner_ports[1] = tcb.remote_port;
    *reinterpret_cast<uint32_t*>(&icmp_buffer[106]) = slabflux::core::endian::host_to_network32(9999); // Spoofed Seq

    inject_frame(icmp_buffer, 110, 1000);
    EXPECT_EQ(tcb.remote_mss, 1220); // Unchanged!
    
    *reinterpret_cast<uint32_t*>(&icmp_buffer[106]) = slabflux::core::endian::host_to_network32(2000); // Valid Seq
    inject_frame(icmp_buffer, 110, 1000);
    EXPECT_EQ(tcb.remote_mss, 1040); // 1100 - 60 (IPv6+TCP base headers)
}

// ============================================================================
// TCP STATE MACHINE & CVE MITIGATION TESTS
// ============================================================================

TEST_F(PublicGatewaySecurityTest, MitigatesCVE_2019_11479_MssResourceExhaustion) {
    alignas(64) char syn_buffer[128];
    auto* syn = reinterpret_cast<raw_tcp_ipv4_frame*>(syn_buffer);
    init_valid_tcp_base(syn);
    syn->tcp_seq = slabflux::core::endian::host_to_network32(5000);
    
    // Inject MSS Option = 48 bytes (Artificially small)
    syn->tcp_data_offset = 0x60; // 24 bytes (20 + 4 opt)
    uint8_t* opt = reinterpret_cast<uint8_t*>(syn_buffer) + 54;
    opt[0] = 0x02; opt[1] = 0x04; opt[2] = 0x00; opt[3] = 0x30; // MSS 48
    
    syn->ip_len = slabflux::core::endian::host_to_network16(44);
    syn->ip_checksum = 0;
    syn->ip_checksum = slabflux::net::tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(syn) + 14, 20, 0);

    uint32_t pseudo_sum = 0;
    uint32_t src = syn->ip_src;
    uint32_t dst = syn->ip_dst;
    pseudo_sum += (src & 0xFFFF) + (src >> 16);
    pseudo_sum += (dst & 0xFFFF) + (dst >> 16);
    pseudo_sum += slabflux::core::endian::host_to_network16(6);
    pseudo_sum += slabflux::core::endian::host_to_network16(24);
    syn->tcp_checksum = 0;
    syn->tcp_checksum = slabflux::net::tcp_wire_engine::compute_checksum(&syn->tcp_src_port, 24, pseudo_sum);
    
    inject_frame(syn_buffer, 58, 1000);
    
    ASSERT_EQ(egress_ring.available_to_peek(), 1);
    uint32_t cookie = slabflux::core::endian::network_to_host32(
        reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(egress_ring.get_peek_slot(0)->mbuf, void*))->tcp_seq
    );
    egress_ring.consume_n(1);
    
    alignas(64) char ack_buffer[128];
    auto* ack = reinterpret_cast<raw_tcp_ipv4_frame*>(ack_buffer);
    init_valid_tcp_base(ack);
    ack->tcp_flags = FLAG_ACK;
    ack->tcp_seq = slabflux::core::endian::host_to_network32(5001);
    ack->tcp_ack = slabflux::core::endian::host_to_network32(cookie + 1);
    finalize_tcp_checksum(ack);
    
    inject_frame(ack_buffer, 54, 1500);
    
    ASSERT_EQ(accept_ring.available_to_peek(), 1);
    uint32_t conn_id = *accept_ring.get_peek_slot(0);
    accept_ring.consume_n(1);
    
    auto& tcb = gateway.get_tcb(conn_id);
    EXPECT_EQ(tcb.phase_mask, PHASE_ESTABLISHED);
    // Remote MSS MUST be clamped to 536 to prevent resource exhaustion (CVE-2019-11479)
    EXPECT_EQ(tcb.remote_mss, 536);
}

TEST_F(PublicGatewaySecurityTest, MitigatesCVE_2004_0230_BlindResetSpoofing) {
    uint32_t conn_id = gateway.connect_outbound(
        slabflux::core::endian::host_to_network32(0x08080808), 80
    );
    auto& tcb = gateway.get_tcb(conn_id);
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.rcv_nxt = 5000;
    tcb.challenge_ack_cnt = 0;
    
    alignas(64) char rst_buffer[128];
    auto* rst = reinterpret_cast<raw_tcp_ipv4_frame*>(rst_buffer);
    init_valid_tcp_base(rst);
    rst->tcp_flags = FLAG_RST;
    rst->tcp_seq = slabflux::core::endian::host_to_network32(5100); // IN window, but not exact rcv_nxt
    rst->tcp_src_port = tcb.remote_port;
    rst->tcp_dst_port = tcb.local_port;
    rst->ip_len = slabflux::core::endian::host_to_network16(40);
    rst->ip_checksum = 0;
    rst->ip_checksum = slabflux::net::tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(rst) + 14, 20, 0);
    finalize_tcp_checksum(rst);
    
    inject_frame(rst_buffer, 54, 2000);
    
    // Connection MUST remain established (no teardown)
    EXPECT_EQ(tcb.phase_mask, PHASE_ESTABLISHED);
    
    // Gateway MUST have scheduled a Challenge ACK
    ASSERT_EQ(egress_ring.available_to_peek(), 1);
    auto* reply = reinterpret_cast<raw_tcp_ipv4_frame*>(rte_pktmbuf_mtod(egress_ring.get_peek_slot(0)->mbuf, void*));
    EXPECT_EQ(reply->tcp_flags, FLAG_ACK);
    egress_ring.consume_n(1);
    EXPECT_EQ(tcb.challenge_ack_cnt, 1);
}

// ============================================================================
// TCP STATE MACHINE & CVE MITIGATION TESTS
// ============================================================================

TEST_F(PublicGatewaySecurityTest, MitigatesCVE_2008_4609_TimeWaitAssassination) {
    uint32_t conn_id = gateway.connect_outbound(
        slabflux::core::endian::host_to_network32(0x08080808), 80
    );
    auto& tcb = gateway.get_tcb(conn_id);
    tcb.phase_mask = PHASE_TIME_WAIT; 
    tcb.rcv_nxt = 5000;
    
    alignas(64) char rst_buffer[128];
    auto* rst = reinterpret_cast<raw_tcp_ipv4_frame*>(rst_buffer);
    init_valid_tcp_base(rst);
    rst->tcp_flags = FLAG_RST;
    rst->tcp_seq = slabflux::core::endian::host_to_network32(5000); // Exact match RST
    rst->tcp_src_port = tcb.remote_port;
    rst->tcp_dst_port = tcb.local_port;
    finalize_tcp_checksum(rst);
    
    inject_frame(rst_buffer, 54, 2000);
    
    // RST MUST be completely ignored in TIME_WAIT to ensure 2MSL completes.
    EXPECT_EQ(tcb.phase_mask, PHASE_TIME_WAIT);
}

// ============================================================================
// IP DEFRAGMENTATION & REASSEMBLY TESTS (UDP)
// ============================================================================

TEST_F(PublicGatewaySecurityTest, ReassemblesFragmentedUdp) {
    alignas(64) char frag1[128];
    alignas(64) char frag2[128];
    
    std::memset(frag1, 0, sizeof(frag1));
    std::memset(frag2, 0, sizeof(frag2));

    auto* hdr1 = reinterpret_cast<raw_tcp_ipv4_frame*>(frag1);
    auto* hdr2 = reinterpret_cast<raw_tcp_ipv4_frame*>(frag2);

    for (auto* hdr : {hdr1, hdr2}) {
        hdr->eth_type = slabflux::core::endian::host_to_network16(0x0800);
        hdr->ip_ihl_ver = 0x45;
        hdr->ip_ttl = 64;
        hdr->ip_protocol = 17; // UDP
        hdr->ip_src = slabflux::core::endian::host_to_network32(0x08080808);
        hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A000001);
        hdr->ip_id = slabflux::core::endian::host_to_network16(0x1234);
        std::memcpy(hdr->dest_mac, "\x00\x11\x22\x33\x44\x55", 6);
    }

    // Fragment 1: 16 bytes (8 UDP header + 8 payload)
    hdr1->ip_len = slabflux::core::endian::host_to_network16(36);
    hdr1->ip_frag_offset = slabflux::core::endian::host_to_network16(0x2000); // MF = 1, Offset = 0
    hdr1->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr1) + 14, 20, 0);
    
    uint16_t* udp_hdr = reinterpret_cast<uint16_t*>(&frag1[34]);
    udp_hdr[0] = slabflux::core::endian::host_to_network16(12345);
    udp_hdr[1] = slabflux::core::endian::host_to_network16(53);
    udp_hdr[2] = slabflux::core::endian::host_to_network16(24);
    udp_hdr[3] = 0; 
    std::memcpy(&frag1[42], "AAAAAAAA", 8);

    // Fragment 2: 8 bytes payload, Offset = 2 (16 bytes)
    hdr2->ip_len = slabflux::core::endian::host_to_network16(28);
    hdr2->ip_frag_offset = slabflux::core::endian::host_to_network16(2); // MF = 0, Offset = 2
    hdr2->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr2) + 14, 20, 0);
    std::memcpy(&frag2[34], "BBBBBBBB", 8);

    inject_frame(frag1, 50, 1000); 
    EXPECT_EQ(defrag.udp_datagram_calls, 0); 

    inject_frame(frag2, 42, 1000); 
    EXPECT_EQ(defrag.udp_datagram_calls, 1); 
}

TEST_F(PublicGatewaySecurityTest, DropsTeardropOverlappingFragments) {
    alignas(64) char frag1[128];
    alignas(64) char frag2[128];
    
    std::memset(frag1, 0, sizeof(frag1));
    std::memset(frag2, 0, sizeof(frag2));

    auto* hdr1 = reinterpret_cast<raw_tcp_ipv4_frame*>(frag1);
    auto* hdr2 = reinterpret_cast<raw_tcp_ipv4_frame*>(frag2);

    for (auto* hdr : {hdr1, hdr2}) {
        hdr->eth_type = slabflux::core::endian::host_to_network16(0x0800);
        hdr->ip_ihl_ver = 0x45;
        hdr->ip_ttl = 64;
        hdr->ip_protocol = 17;
        hdr->ip_src = slabflux::core::endian::host_to_network32(0x08080808);
        hdr->ip_dst = slabflux::core::endian::host_to_network32(0x0A000001);
        hdr->ip_id = slabflux::core::endian::host_to_network16(0x9999);
        std::memcpy(hdr->dest_mac, "\x00\x11\x22\x33\x44\x55", 6);
    }

    hdr1->ip_len = slabflux::core::endian::host_to_network16(36);
    hdr1->ip_frag_offset = slabflux::core::endian::host_to_network16(0x2000); 
    hdr1->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr1) + 14, 20, 0);
    std::memcpy(&frag1[34], "1234567812345678", 16);

    // Malicious Overlap: Offset 1 (8 bytes) overlaps with the 16 bytes of Frag 1
    hdr2->ip_len = slabflux::core::endian::host_to_network16(28);
    hdr2->ip_frag_offset = slabflux::core::endian::host_to_network16(1); 
    hdr2->ip_checksum = tcp_wire_engine::compute_checksum(reinterpret_cast<char*>(hdr2) + 14, 20, 0);
    std::memcpy(&frag2[34], "OVERLAP!", 8);

    inject_frame(frag1, 50, 1000);
    EXPECT_EQ(defrag.udp_datagram_calls, 0);

    inject_frame(frag2, 42, 1000);
    EXPECT_EQ(defrag.udp_datagram_calls, 0); // Teardrop explicitly rejected
}
