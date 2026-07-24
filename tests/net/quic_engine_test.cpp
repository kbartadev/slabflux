/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * @file quic_engine_test.cpp
 * @brief Unit tests for the Zero-Allocation QUIC Engine.
 */

#include <gtest/gtest.h>
#include <cstring>
#include "slabflux/net/quic_engine.hpp"

using namespace slabflux::net;

struct MockQuicGateway {
    uint16_t get_local_port() const { return 443; }
    bool send_udp(uint32_t, uint16_t, uint16_t, const char*, size_t) { return true; }
    bool send_udp_ipv6(const uint64_t[2], uint16_t, uint16_t, const char*, size_t) { return true; }
};

// ============================================================================
// TEST 1: Bounds Checking & Malformed Rejector
// ============================================================================
TEST(QuicEngineTest, RejectsInvalidPackets) {
    MockQuicGateway gw;
    auto engine = std::make_unique<quic_engine<MockQuicGateway>>(gw, 443);

    uint64_t src_ip[2] = {0, 0};
    
    // Completely truncated QUIC datagram
    char short_buf[10] = {0};
    engine->process_inbound_quic(short_buf, 10, src_ip, 12345); // Should cleanly return without faulting
    
    // Invalid Long Header (Version Negotiation packet should be ignored gracefully)
    char vn_buf[64] = {0};
    vn_buf[0] = 0x80; // Long Header
    vn_buf[1] = 0; vn_buf[2] = 0; vn_buf[3] = 0; vn_buf[4] = 0; // Version 0
    engine->process_inbound_quic(vn_buf, 64, src_ip, 12345);
}

// ============================================================================
// TEST 2: Zero-Allocation VarInt Decoder (RFC 9000 16.)
// ============================================================================
TEST(QuicEngineTest, VarIntDecoding) {
    uint8_t buf1[] = {0x25}; // 1-byte encoding (37)
    const uint8_t* ptr = buf1;
    EXPECT_EQ(decode_quic_varint(&ptr, buf1 + 1), 37);
    
    uint8_t buf2[] = {0x7b, 0xbd}; // 2-byte encoding (15293)
    ptr = buf2;
    EXPECT_EQ(decode_quic_varint(&ptr, buf2 + 2), 15293);
    
    uint8_t buf4[] = {0x9d, 0x7f, 0x3e, 0x7d}; // 4-byte encoding (494878333)
    ptr = buf4;
    EXPECT_EQ(decode_quic_varint(&ptr, buf4 + 4), 494878333);
    
    uint8_t buf8[] = {0xc2, 0x19, 0x7c, 0x5e, 0xff, 0x14, 0xe8, 0x8c}; // 8-byte encoding
    ptr = buf8;
    EXPECT_EQ(decode_quic_varint(&ptr, buf8 + 8), 151288809941952652ULL);
}