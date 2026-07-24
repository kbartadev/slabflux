/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * @file ipv6_logic_test.cpp
 * @brief High-fidelity algorithmic and structural validation of IPv6 mechanics.
 */

#include <gtest/gtest.h>
#include "slabflux/net/raw_tcp_ipv6_frame.hpp"
#include "slabflux/net/raw_udp_ipv6_frame.hpp"
#include "slabflux/net/tcp_wire_engine.hpp"
#include "slabflux/net/tcp_syn_cookie.hpp"
#include "slabflux/core/endian.hpp"
#include <cstring>

using namespace slabflux::net;

// ============================================================================
// TEST 1: Structural L1 Geometry Invariants
// Validates that the packed frames map identically to physical 64/128B boundaries.
// ============================================================================
TEST(IPv6LogicTest, FrameLayoutAssertions) {
    EXPECT_EQ(sizeof(raw_tcp_ipv6_frame), 128) << "TCP IPv6 frame must perfectly tile into 128 bytes (2x L1 Cache Lines)";
    EXPECT_EQ(offsetof(raw_tcp_ipv6_frame, ipv6_flow), 14) << "IPv6 header must start at physical offset 14";
    EXPECT_EQ(offsetof(raw_tcp_ipv6_frame, tcp_src_port), 54) << "TCP header must start at physical offset 54";

    EXPECT_EQ(sizeof(raw_udp_ipv6_frame), 64) << "UDP IPv6 frame must perfectly tile into 64 bytes (1x L1 Cache Line)";
    EXPECT_EQ(offsetof(raw_udp_ipv6_frame, udp_src_port), 54) << "UDP header must start at physical offset 54";
}

// ============================================================================
// TEST 2: Checksum Cohomology & Hardware Emulation
// Validates that 1's complement software folds execute accurately over IPv6
// pseudo-headers and correctly resolve to 0 when re-evaluated.
// ============================================================================
TEST(IPv6LogicTest, ChecksumComputation) {
    alignas(64) raw_tcp_ipv6_frame frame{};
    std::memset(&frame, 0, sizeof(frame));

    frame.ipv6_src[0] = slabflux::core::endian::host_to_network64(0x20010db885a30000ULL);
    frame.ipv6_src[1] = slabflux::core::endian::host_to_network64(0x8a2e037073340000ULL);
    frame.ipv6_dst[0] = slabflux::core::endian::host_to_network64(0xfe80000000000000ULL);
    frame.ipv6_dst[1] = slabflux::core::endian::host_to_network64(0x0202b3fffe1e8329ULL);
    frame.tcp_src_port = slabflux::core::endian::host_to_network16(0x1234);
    frame.tcp_dst_port = slabflux::core::endian::host_to_network16(0x5678);
    frame.tcp_seq = slabflux::core::endian::host_to_network32(0x11111111);
    frame.tcp_ack = slabflux::core::endian::host_to_network32(0x22222222);
    frame.tcp_data_offset = 0x50; // 20 bytes
    frame.tcp_flags = 0x02; // SYN
    
    uint16_t tcp_len = 20;
    uint32_t pseudo_sum = 0;
    const uint16_t* src16 = reinterpret_cast<const uint16_t*>(frame.ipv6_src);
    const uint16_t* dst16 = reinterpret_cast<const uint16_t*>(frame.ipv6_dst);
    
    for(int i = 0; i < 8; ++i) pseudo_sum += src16[i];
    for(int i = 0; i < 8; ++i) pseudo_sum += dst16[i];
    
    pseudo_sum += slabflux::core::endian::host_to_network16(6);
    pseudo_sum += slabflux::core::endian::host_to_network16(tcp_len);
    
    uint16_t csum = tcp_wire_engine::compute_checksum(&frame.tcp_src_port, tcp_len, pseudo_sum);
    
    EXPECT_NE(csum, 0);
    EXPECT_NE(csum, 0xFFFF);
    
    // Validate Mathematical Invariance: applying the generated checksum and recomputing MUST yield 0.
    frame.tcp_checksum = csum;
    uint16_t verify_csum = tcp_wire_engine::compute_checksum(&frame.tcp_src_port, tcp_len, pseudo_sum);
    EXPECT_EQ(verify_csum, 0);
}

TEST(IPv6LogicTest, UdpChecksumComputation) {
    alignas(64) char buffer[128];
    auto* frame = reinterpret_cast<raw_udp_ipv6_frame*>(buffer);
    std::memset(buffer, 0, sizeof(buffer));

    frame->ipv6_src[0] = slabflux::core::endian::host_to_network64(0x20010db885a30000ULL);
    frame->ipv6_src[1] = slabflux::core::endian::host_to_network64(0x8a2e037073340000ULL);
    frame->ipv6_dst[0] = slabflux::core::endian::host_to_network64(0xfe80000000000000ULL);
    frame->ipv6_dst[1] = slabflux::core::endian::host_to_network64(0x0202b3fffe1e8329ULL);
    frame->udp_src_port = slabflux::core::endian::host_to_network16(0x1234);
    frame->udp_dst_port = slabflux::core::endian::host_to_network16(0x5678);
    
    char payload[] = "UDP_CHECKSUM_TEST";
    size_t payload_len = sizeof(payload) - 1;
    uint16_t udp_len = 8 + payload_len;
    frame->udp_length = slabflux::core::endian::host_to_network16(udp_len);
    std::memcpy(buffer + 62, payload, payload_len); // Place payload after UDP header

    uint32_t pseudo_sum = 0;
    const uint16_t* src16 = reinterpret_cast<const uint16_t*>(frame->ipv6_src);
    const uint16_t* dst16 = reinterpret_cast<const uint16_t*>(frame->ipv6_dst);
    
    for(int i = 0; i < 8; ++i) pseudo_sum += src16[i];
    for(int i = 0; i < 8; ++i) pseudo_sum += dst16[i];
    
    pseudo_sum += slabflux::core::endian::host_to_network16(17); // UDP Protocol
    pseudo_sum += frame->udp_length;
    
    uint16_t csum = tcp_wire_engine::compute_checksum(&frame->udp_src_port, udp_len, pseudo_sum);
    if (csum == 0) csum = 0xFFFF; // RFC 768
    
    frame->udp_checksum = csum;
    uint16_t verify_csum = tcp_wire_engine::compute_checksum(&frame->udp_src_port, udp_len, pseudo_sum);
    EXPECT_EQ(verify_csum, 0);
}

// ============================================================================
// TEST 3: Cryptographic IPv6 SYN Cookie Validation
// Verifies AES-128 MAC generation, temporal truncation limits, and parameter mapping.
// ============================================================================
TEST(IPv6LogicTest, SynCookieV6GenerationAndValidation) {
    tcp_syn_cookie::seed_keys();
    
    uint64_t src[2] = {0x1111, 0x2222};
    uint64_t dst[2] = {0x3333, 0x4444};
    uint32_t client_isn = 12345;
    uint16_t client_mss = 1220;
    uint64_t current_time_ms = 1000;

    uint32_t cookie = tcp_syn_cookie::generate_ipv6(
        src, 8080, dst, 443, client_isn, client_mss, current_time_ms
    );

    EXPECT_NE(cookie, 0);

    auto status = tcp_syn_cookie::validate_ipv6(
        src, 8080, dst, 443, cookie, client_isn, current_time_ms
    );
    
    EXPECT_TRUE(status.is_valid);
    // The MSS binning logic drops 1220 down to the nearest floor bucket (1024)
    EXPECT_EQ(status.mss, 1024); 
}

TEST(IPv6LogicTest, SynCookieV6ExpirationAndTampering) {
    tcp_syn_cookie::seed_keys();
    uint64_t src[2] = {0xAAAA, 0xBBBB};
    uint64_t dst[2] = {0xCCCC, 0xDDDD};
    uint32_t client_isn = 99999;
    uint64_t current_time_ms = 1000;
    
    uint32_t cookie = tcp_syn_cookie::generate_ipv6(src, 1234, dst, 443, client_isn, 1460, current_time_ms);
    
    // 1. Ensure expired cookies fail (advance by > 2 periods of 64s, ~130 seconds)
    EXPECT_FALSE(tcp_syn_cookie::validate_ipv6(src, 1234, dst, 443, cookie, client_isn, current_time_ms + 130000).is_valid);
    
    // 2. Ensure structurally tampered tuples fail AES CBC-MAC verification
    uint64_t tampered_src[2] = {0xAAAA, 0x0000};
    EXPECT_FALSE(tcp_syn_cookie::validate_ipv6(tampered_src, 1234, dst, 443, cookie, client_isn, current_time_ms).is_valid);
}