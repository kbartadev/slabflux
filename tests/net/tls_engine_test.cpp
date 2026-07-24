/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * @file tls_engine_test.cpp
 * @brief Unit tests for TLS 1.3 engines, Virtual TLS Sockets, and HKDF-SHA256.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <string_view>

#include <rte_mbuf.h>

// Mock DPDK Mempool Allocations for Unit Testing
inline struct rte_mbuf* mock_mbuf_alloc_tls(struct rte_mempool*) {
    char* mem = new char[sizeof(struct rte_mbuf) + 2048];
    std::memset(mem, 0, sizeof(struct rte_mbuf) + 2048);
    struct rte_mbuf* m = reinterpret_cast<struct rte_mbuf*>(mem);
    m->buf_addr = mem + sizeof(struct rte_mbuf);
    m->data_off = 0;
    m->pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    rte_mbuf_refcnt_set(m, 1);
    return m;
}

inline void mock_mbuf_free_tls(struct rte_mbuf* m) {
    if (m) {
        if (rte_mbuf_refcnt_read(m) <= 1) delete[] reinterpret_cast<char*>(m);
        else rte_mbuf_refcnt_update(m, -1);
    }
}

#undef rte_pktmbuf_alloc
#undef rte_pktmbuf_free
#define rte_pktmbuf_alloc mock_mbuf_alloc_tls
#define rte_pktmbuf_free mock_mbuf_free_tls

#include "slabflux/net/virtual_tls_socket.hpp"
#include "slabflux/net/hkdf_sha256.hpp"
#include "slabflux/sys/tls_accelerator.hpp"

using namespace slabflux::net;

class TlsEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!__builtin_cpu_supports("pclmul") || !__builtin_cpu_supports("aes")) {
            GTEST_SKIP() << "Skipping AES-GCM tests: AES-NI / PCLMULQDQ not supported by host silicon.";
        }
    }
};

// ============================================================================
// TEST 1: RFC 5869 HKDF-SHA256 Test Vector
// ============================================================================
TEST_F(TlsEngineTest, HkdfSha256_Rfc5869_TestCase1) {
    uint8_t ikm[22]; std::memset(ikm, 0x0b, 22);
    uint8_t salt[13] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
    uint8_t info[10] = {0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9};
    
    uint8_t prk[32];
    hkdf_sha256::extract(salt, 13, ikm, 22, prk);
    
    uint8_t expected_prk[32] = {
        0x07, 0x77, 0x09, 0x36, 0x2c, 0x2e, 0x32, 0xdf, 0x0d, 0xdc, 0x3f, 0x0d, 0xc4, 0x7b, 0xba, 0x63,
        0x90, 0xb6, 0xc7, 0x3b, 0xb5, 0x0f, 0x9c, 0x31, 0x22, 0xec, 0x84, 0x4a, 0xd7, 0xc2, 0xb3, 0xe5
    };
    EXPECT_EQ(std::memcmp(prk, expected_prk, 32), 0);
    
    uint8_t okm[42];
    hkdf_sha256::expand(prk, info, 10, okm, 42);
    
    uint8_t expected_okm[42] = {
        0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a, 0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
        0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c, 0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf,
        0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18, 0x58, 0x65
    };
    EXPECT_EQ(std::memcmp(okm, expected_okm, 42), 0);
}

// ============================================================================
// TEST 2: Virtual TLS Socket Outbound Encryption (L7 to L4 Flow)
// ============================================================================
TEST_F(TlsEngineTest, VirtualTlsSocket_EncryptsAndSends) {
    tcp_transmission_control_block tcb{};
    tcb.phase_mask = PHASE_ESTABLISHED;
    tcb.snd_wnd = 65535;
    tcb.cwnd = 65535;
    
    slabflux::core::spsc_ring_conduit<outbound_tcp_segment<1460>, 1024> tx_ring;
    tcb.tx_egress_conduit = &tx_ring;
    tcb.tx_mbuf_pool = reinterpret_cast<struct rte_mempool*>(0xDEADBEEF);
    
    virtual_tcp_socket tcp_sock(&tcb);
    virtual_tls_socket tls_sock(tcp_sock);
    
    uint8_t c_key[16] = {0}; uint8_t c_iv[12] = {0};
    uint8_t s_key[16] = {0}; uint8_t s_iv[12] = {0};
    tls_sock.set_traffic_keys(c_key, c_iv, s_key, s_iv, false);
    
    const char* payload = "Hello Secure World!";
    size_t payload_len = std::strlen(payload);
    
    ssize_t sent = tls_sock.send(payload, payload_len);
    
    // Expected Length: 5 bytes TLS header + payload_len + 1 byte InnerType + 16 bytes Tag
    size_t expected_sent = 5 + payload_len + 1 + 16;
    EXPECT_EQ(sent, expected_sent);
    EXPECT_EQ(tx_ring.available_to_peek(), 1);
}

TEST_F(TlsEngineTest, TlsAccelerator_SupportCheck) {
    EXPECT_TRUE(slabflux::sys::tls_accelerator::is_supported());
}