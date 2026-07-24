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
 * ============================================================================* @file tls_handshake_machine_test.cpp
 * @brief Unit tests for the Zero-Allocation TLS 1.3 Handshake Machine.
 */

#include <gtest/gtest.h>
#include "slabflux/security/tls_handshake_machine.hpp"
#include "slabflux/security/tls_record_layer.hpp"
#include "slabflux/net/virtual_tcp_socket.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"
#include <vector>
#include <string_view>

using namespace slabflux::security;
using namespace slabflux::net;
using namespace slabflux::core;

// Mock the embedded certificate symbols for the linker to satisfy the zero-allocation 
// embedded_certificate accessor during the Handshake tests.
#if defined(__GNUC__) || defined(__clang__)
asm (
       
    ".global _binary_server_cert_der_start\n"
    ".global _binary_server_cert_der_end\n"
    "_binary_server_cert_der_start:\n"
    ".byte 0x30, 0x61\n"
    ".byte 0x30, 0x5F\n"
    ".byte 0x02, 0x01, 0x01\n"
    ".byte 0x30, 0x02, 0x00, 0x00\n"
    ".byte 0x30, 0x02, 0x00, 0x00\n"
    ".byte 0x30, 0x02, 0x00, 0x00\n"
    ".byte 0x30, 0x02, 0x00, 0x00\n"
    ".byte 0x30, 0x48\n"
    ".byte 0x30, 0x02, 0x00, 0x00\n"
    ".byte 0x03, 0x42, 0x00\n"
    ".byte 0x04\n"
    ".fill 64, 1, 0xAA\n"


    ".global _binary_client_cert_der_start\n"
    ".global _binary_client_cert_der_end\n"
    "_binary_client_cert_der_start:\n"
    ".byte 0x30, 0x61\n"
    ".byte 0x30, 0x5F\n"
    ".byte 0x02, 0x01, 0x01\n"
    ".byte 0x30, 0x02, 0x00, 0x00\n"
    ".byte 0x30, 0x02, 0x00, 0x00\n"
    ".byte 0x30, 0x02, 0x00, 0x00\n"
    ".byte 0x30, 0x02, 0x00, 0x00\n"
    ".byte 0x30, 0x48\n"
    ".byte 0x30, 0x02, 0x00, 0x00\n"
    ".byte 0x03, 0x42, 0x00\n"
    ".byte 0x04\n"
    ".fill 64, 1, 0xBB\n"
    "_binary_client_cert_der_end:\n"
);
#endif


TEST(TlsHandshakeMachineTest, ForgedClientCertificateVerifyHaltsHandshake) {
    tls_crypto_registry<> client_reg;
    tls_crypto_registry<> server_reg;

    auto& c_session = client_reg.get_session(1);
    c_session.hs_ctx.state = tls_handshake_state::GENERATE_CLIENT_HELLO;

    auto& s_session = server_reg.get_session(2);
    s_session.hs_ctx.state = tls_handshake_state::EXPECT_CLIENT_HELLO;

    alignas(64) char c_scratch[4096];
    alignas(64) char s_scratch[4096];

    // 1. Client generates ClientHello
    size_t c_len1 = tls_handshake_machine::process_handshake(c_session.keys, c_session.hs_ctx, "", c_scratch, sizeof(c_scratch));
    ASSERT_GT(c_len1, 5);
    
    uint16_t ch_rec_len = (static_cast<uint8_t>(c_scratch[3]) << 8) | static_cast<uint8_t>(c_scratch[4]);
    std::string_view ch_payload(c_scratch + 5, ch_rec_len);

    // 2. Server processes ClientHello, generates ServerHello + EncryptedExtensions (which requests mTLS)
    size_t s_len1 = tls_handshake_machine::process_handshake(s_session.keys, s_session.hs_ctx, ch_payload, s_scratch, sizeof(s_scratch));
    ASSERT_GT(s_len1, 5);
    
    // Parse Server's records
    uint16_t sh_rec_len = (static_cast<uint8_t>(s_scratch[3]) << 8) | static_cast<uint8_t>(s_scratch[4]);
    std::string_view sh_payload(s_scratch + 5, sh_rec_len);
    
    // Feed ServerHello to Client
    tls_handshake_machine::process_handshake(c_session.keys, c_session.hs_ctx, sh_payload, c_scratch, sizeof(c_scratch));
    EXPECT_EQ(c_session.hs_ctx.state, tls_handshake_state::EXPECT_SERVER_FINISHED);
    
    size_t rec2_start = 5 + sh_rec_len;
    uint16_t ee_rec_len = (static_cast<uint8_t>(s_scratch[rec2_start + 3]) << 8) | static_cast<uint8_t>(s_scratch[rec2_start + 4]);
    char* ee_ciphertext = s_scratch + rec2_start + 5;
    
    size_t ee_plain_len = aes_gcm_hardware::decrypt_in_place(c_session.keys.server_write_key, c_session.keys.server_write_iv, 0, ee_ciphertext, ee_rec_len);
    ASSERT_GT(ee_plain_len, 0);

    // Feed decrypted EncryptedExtensions to Client
    size_t c_len2 = tls_handshake_machine::process_handshake(c_session.keys, c_session.hs_ctx, std::string_view(ee_ciphertext, ee_plain_len - 1), c_scratch, sizeof(c_scratch));
    ASSERT_GT(c_len2, 5);
    EXPECT_EQ(c_session.hs_ctx.state, tls_handshake_state::ESTABLISHED);
    
    // c_scratch contains the Client's encrypted response: Certificate + CertificateVerify + Finished
    uint16_t c_flight_rec_len = (static_cast<uint8_t>(c_scratch[3]) << 8) | static_cast<uint8_t>(c_scratch[4]);
    char* c_flight_cipher = c_scratch + 5;

    // Copy client write keys to decrypt and re-encrypt locally
    __m128i test_write_key[11];
    for (int i = 0; i < 11; ++i) test_write_key[i] = c_session.keys.client_write_key[i];
    uint8_t test_write_iv[12];
    std::memcpy(test_write_iv, c_session.keys.client_write_iv, 12);

    size_t c_flight_plain_len = aes_gcm_hardware::decrypt_in_place(test_write_key, test_write_iv, 0, c_flight_cipher, c_flight_rec_len);
    ASSERT_GT(c_flight_plain_len, 0);

    // Mutate the signature in the decrypted flight
    size_t pos = 0;
    bool forged = false;
    while (pos + 4 <= c_flight_plain_len - 1) {
        uint8_t msg_type = c_flight_cipher[pos];
        uint32_t msg_len = (static_cast<uint8_t>(c_flight_cipher[pos+1]) << 16) |
                           (static_cast<uint8_t>(c_flight_cipher[pos+2]) << 8) |
                            static_cast<uint8_t>(c_flight_cipher[pos+3]);
        
        if (msg_type == 0x0F) { // CertificateVerify
            c_flight_cipher[pos + 8] ^= 0xFF; // Corrupt the ASN.1 Sequence Tag directly
            forged = true;
            break;
        }
        pos += 4 + msg_len;
    }
    ASSERT_TRUE(forged);

    // Re-encrypt the corrupted flight
    aes_gcm_hardware::encrypt_in_place(test_write_key, test_write_iv, 0, c_flight_cipher, c_flight_plain_len);

    // Feed corrupted flight to Server's EXPECT_FINISHED state
    tls_handshake_machine::process_handshake(s_session.keys, s_session.hs_ctx, std::string_view(c_flight_cipher, c_flight_rec_len), s_scratch, sizeof(s_scratch));

    // Validation: The Server must immediately reject the forged signature and fail out
    EXPECT_EQ(s_session.hs_ctx.state, tls_handshake_state::FAILED);
}