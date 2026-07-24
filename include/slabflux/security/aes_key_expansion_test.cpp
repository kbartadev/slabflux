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
 * ============================================================================* @file aes_key_expansion_test.cpp
 * @brief Validation of hardware AES-128 Key Expansion against NIST FIPS-197.
 */

#include <gtest/gtest.h>
#include <cstdint>
#include <immintrin.h>
#include "slabflux/security/aes_gcm_hardware.hpp"

using namespace slabflux::security;

TEST(AesGcmHardwareTest, NistFips197KeyExpansion) {
    // FIPS-197 Appendix A.1: AES-128 Test Vector Initial Key
    const uint8_t nist_key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };

    // Expected Round Keys (0-10) derived strictly from the FIPS-197 standard
    const uint8_t expected_rounds[11][16] = {
        {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c}, // Round 0 (Original Key)
        {0xa0, 0xfa, 0xfe, 0x17, 0x88, 0x54, 0x2c, 0xb1, 0x23, 0xa3, 0x39, 0x39, 0x2a, 0x6c, 0x76, 0x05}, // Round 1
        {0xf2, 0xc2, 0x95, 0xf2, 0x7a, 0x96, 0xb9, 0x43, 0x59, 0x35, 0x80, 0x7a, 0x73, 0x59, 0xf6, 0x7f}, // Round 2
        {0x3d, 0x80, 0x47, 0x7d, 0x47, 0x16, 0xfe, 0x3e, 0x1e, 0x23, 0x7e, 0x44, 0x6d, 0x7a, 0x88, 0x3b}, // Round 3
        {0xef, 0x44, 0xa5, 0x41, 0xa8, 0x52, 0x5b, 0x7f, 0xb6, 0x71, 0x25, 0x3b, 0xdb, 0x0b, 0xad, 0x00}, // Round 4
        {0xd4, 0xd1, 0xc6, 0xf8, 0x7c, 0x83, 0x9d, 0x87, 0xca, 0xf2, 0xb8, 0xbc, 0x11, 0xf9, 0x15, 0xbc}, // Round 5
        {0x6d, 0x88, 0xa3, 0x7a, 0x11, 0x0b, 0x3e, 0xfd, 0xdb, 0xf9, 0x86, 0x41, 0xca, 0x00, 0x93, 0xfd}, // Round 6
        {0x4e, 0x54, 0xf7, 0x0e, 0x5f, 0x5f, 0xc9, 0xf3, 0x84, 0xa6, 0x4f, 0xb2, 0x4e, 0xa6, 0xdc, 0x4f}, // Round 7
        {0xea, 0xd2, 0x73, 0x21, 0xb5, 0x8d, 0xba, 0xd2, 0x31, 0x2b, 0xf5, 0x60, 0x7f, 0x8d, 0x29, 0x2f}, // Round 8
        {0x43, 0x49, 0x36, 0x5e, 0xf6, 0xc4, 0x8a, 0x8c, 0xc7, 0xec, 0x7f, 0xec, 0xb8, 0x61, 0x56, 0xc3}, // Round 9
        {0xd0, 0x14, 0xf9, 0xa8, 0xc9, 0xee, 0x25, 0x89, 0xe1, 0x3f, 0x0c, 0xc8, 0xb6, 0x63, 0x0c, 0xa6}  // Round 10
    };

    // Output buffer for the 11 hardware round keys
    alignas(16) __m128i generated_round_keys[11];
    
    // Execute zero-allocation inline hardware expansion
    aes_gcm_hardware::expand_key(nist_key, generated_round_keys);

    // Verify each round key mathematically matches the standard
    for (int i = 0; i < 11; ++i) {
        alignas(16) uint8_t round_output[16];
        _mm_store_si128(reinterpret_cast<__m128i*>(round_output), generated_round_keys[i]);

        for (int j = 0; j < 16; ++j) {
            EXPECT_EQ(round_output[j], expected_rounds[i][j]) 
                << "Cryptographic mismatch at Round " << i << ", Byte " << j;
        }
    }
}