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
 * ============================================================================* @file aes_gcm_hardware.hpp
 * @brief Zero-Allocation Hardware AES-GCM (TLS 1.3 Data Path).
 * @details Bypasses OpenSSL overhead by executing raw AES-NI and CLMUL 
 * intrinsics directly on the L1 cache.
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <immintrin.h>
#include <cstring>
#include <wmmintrin.h> // For _mm_clmulepi64_si128
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/endian.hpp"

namespace slabflux::security {

    struct alignas(64) tls_session_keys {
        __m128i client_write_key[11]; // AES-128 Key Schedule
        __m128i server_write_key[11];
        uint8_t client_write_iv[12];
        uint8_t server_write_iv[12];
        uint64_t client_seq{0};
        uint64_t server_seq{0};
        bool active{false};
    };

    struct aes_gcm_hardware {
        
        /**
         * @brief Hardware AES-128 Key Schedule Generation.
         * @details Utilizes AES-NI `_mm_aeskeygenassist_si128` to synthesize the 11 round keys 
         * directly inline, replacing slow software S-Box lookups.
         */
        SLAB_FORCE_INLINE static __m128i key_expansion_step(__m128i key, __m128i keygened) noexcept {
            keygened = _mm_shuffle_epi32(keygened, _MM_SHUFFLE(3, 3, 3, 3));
            key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
            key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
            key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
            return _mm_xor_si128(key, keygened);
        }

        static SLAB_HOT void expand_key(const uint8_t raw_key[16], __m128i round_keys[11]) noexcept {
            __m128i key = _mm_loadu_si128(reinterpret_cast<const __m128i*>(raw_key));
            round_keys[0]  = key;
            round_keys[1]  = key = key_expansion_step(key, _mm_aeskeygenassist_si128(key, 0x01));
            round_keys[2]  = key = key_expansion_step(key, _mm_aeskeygenassist_si128(key, 0x02));
            round_keys[3]  = key = key_expansion_step(key, _mm_aeskeygenassist_si128(key, 0x04));
            round_keys[4]  = key = key_expansion_step(key, _mm_aeskeygenassist_si128(key, 0x08));
            round_keys[5]  = key = key_expansion_step(key, _mm_aeskeygenassist_si128(key, 0x10));
            round_keys[6]  = key = key_expansion_step(key, _mm_aeskeygenassist_si128(key, 0x20));
            round_keys[7]  = key = key_expansion_step(key, _mm_aeskeygenassist_si128(key, 0x40));
            round_keys[8]  = key = key_expansion_step(key, _mm_aeskeygenassist_si128(key, 0x80));
            round_keys[9]  = key = key_expansion_step(key, _mm_aeskeygenassist_si128(key, 0x1B));
            round_keys[10] = key = key_expansion_step(key, _mm_aeskeygenassist_si128(key, 0x36));
        }

        /**
         * @brief Fast Galois Hash Reduction.
         * @details Computes a * b mod (x^128 + x^7 + x^2 + x + 1) natively using CLMUL.
         */
        SLAB_FORCE_INLINE static __m128i ghash_mul(__m128i a, __m128i b) noexcept {
            __m128i tmp3, tmp4, tmp5, tmp6, tmp7, tmp8, tmp9;
            tmp3 = _mm_clmulepi64_si128(a, b, 0x00);
            tmp4 = _mm_clmulepi64_si128(a, b, 0x10);
            tmp5 = _mm_clmulepi64_si128(a, b, 0x01);
            tmp6 = _mm_clmulepi64_si128(a, b, 0x11);
            
            tmp4 = _mm_xor_si128(tmp4, tmp5);
            tmp5 = _mm_slli_si128(tmp4, 8);
            tmp4 = _mm_srli_si128(tmp4, 8);
            tmp3 = _mm_xor_si128(tmp3, tmp5);
            tmp6 = _mm_xor_si128(tmp6, tmp4);
            
            tmp7 = _mm_slli_epi32(tmp3, 31);
            tmp8 = _mm_slli_epi32(tmp3, 30);
            tmp9 = _mm_slli_epi32(tmp3, 25);
            tmp7 = _mm_xor_si128(tmp7, tmp8);
            tmp7 = _mm_xor_si128(tmp7, tmp9);
            tmp8 = _mm_srli_si128(tmp7, 4);
            tmp7 = _mm_slli_si128(tmp7, 12);
            tmp3 = _mm_xor_si128(tmp3, tmp7);
            tmp6 = _mm_xor_si128(tmp6, tmp8);
            
            tmp7 = _mm_srli_epi32(tmp3, 1);
            tmp8 = _mm_srli_epi32(tmp3, 2);
            tmp9 = _mm_srli_epi32(tmp3, 7);
            tmp7 = _mm_xor_si128(tmp7, tmp8);
            tmp7 = _mm_xor_si128(tmp7, tmp9);
            tmp8 = _mm_slli_si128(tmp7, 12);
            tmp7 = _mm_srli_si128(tmp7, 4);
            tmp3 = _mm_xor_si128(tmp3, tmp7);
            tmp6 = _mm_xor_si128(tmp6, tmp8);
            tmp6 = _mm_xor_si128(tmp6, tmp3);
            return tmp6;
        }

        /**
         * @brief In-Place Decryption and Authentication (RFC 8446).
         * @details Decrypts the ciphertext over itself to avoid allocating plaintext buffers.
         * @return Length of the authenticated plaintext, or 0 if the GHASH tag is forged.
         */
        SLAB_HOT static size_t decrypt_in_place(
            const __m128i* round_keys,
            const uint8_t* iv,
            uint64_t sequence_number,
            char* in_out_buffer,
            size_t length
        ) noexcept {
            if (SL_EXPECT_FALSE(length < 16)) return 0; // Missing Auth Tag
            
            const size_t ciphertext_len = length - 16;
            
            // 1. Construct the TLS 1.3 Per-Record Nonce (IV XOR Sequence Number)
            alignas(16) uint8_t nonce[16] = {0};
            __builtin_memcpy(nonce, iv, 12);
            uint64_t net_seq = core::endian::network_to_host64(sequence_number);
            *reinterpret_cast<uint64_t*>(nonce + 4) ^= net_seq;
            nonce[15] = 1; // Counter block initialization

            __m128i counter = _mm_load_si128(reinterpret_cast<const __m128i*>(nonce));
            
            // Generate GHASH subkey (H = E_K(0))
            __m128i H = _mm_setzero_si128();
            H = _mm_xor_si128(H, round_keys[0]);
            for (int i = 1; i < 10; ++i) H = _mm_aesenc_si128(H, round_keys[i]);
            H = _mm_aesenclast_si128(H, round_keys[10]);

            const __m128i bswap_mask = _mm_setr_epi8(15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0);
            H = _mm_shuffle_epi8(H, bswap_mask);

            // Generate initial E(K, J0) for final tag masking
            __m128i e_j0 = _mm_xor_si128(counter, round_keys[0]);
            for (int i = 1; i < 10; ++i) e_j0 = _mm_aesenc_si128(e_j0, round_keys[i]);
            e_j0 = _mm_aesenclast_si128(e_j0, round_keys[10]);

            __m128i ghash_state = _mm_setzero_si128();

            // TLS 1.3 Outer Record Header AAD (Type 23, Version 0x0303, Total Length)
            alignas(16) uint8_t aad_block[16] = {0};
            aad_block[0] = 23; 
            aad_block[1] = 3;  
            aad_block[2] = 3;  
            aad_block[3] = static_cast<uint8_t>(length >> 8);
            aad_block[4] = static_cast<uint8_t>(length & 0xFF);

            __m128i aad_v = _mm_load_si128(reinterpret_cast<const __m128i*>(aad_block));
            aad_v = _mm_shuffle_epi8(aad_v, bswap_mask);
            ghash_state = _mm_xor_si128(ghash_state, aad_v);
            ghash_state = ghash_mul(ghash_state, H);

            // 2. Hardware AES-CTR Decryption Pipeline & GHASH Verification
            size_t offset = 0;
            const __m128i one = _mm_set_epi32(0, 0, 0, 1<<24); // Big-endian 1

            while (offset < ciphertext_len) {
                counter = _mm_add_epi32(counter, one); // Increment CTR
                
                __m128i block = _mm_xor_si128(counter, round_keys[0]);
                for (int i = 1; i < 10; ++i) block = _mm_aesenc_si128(block, round_keys[i]);
                block = _mm_aesenclast_si128(block, round_keys[10]);
                
                size_t chunk = (ciphertext_len - offset >= 16) ? 16 : (ciphertext_len - offset);
                alignas(16) uint8_t temp_cipher[16] = {0};
                __builtin_memcpy(temp_cipher, in_out_buffer + offset, chunk);

                __m128i cipher_block = _mm_load_si128(reinterpret_cast<const __m128i*>(temp_cipher));
                
                // GHASH Accumulation
                __m128i cb_swapped = _mm_shuffle_epi8(cipher_block, bswap_mask);
                ghash_state = _mm_xor_si128(ghash_state, cb_swapped);
                ghash_state = ghash_mul(ghash_state, H);

                // Decrypt and write back
                __m128i plain_block = _mm_xor_si128(cipher_block, block);
                __builtin_memcpy(in_out_buffer + offset, reinterpret_cast<const char*>(&plain_block), chunk);
                
                offset += chunk;
            }

            // 3. Finalize GHASH (Append lengths block)
            alignas(16) uint64_t len_block[2];
            len_block[0] = core::endian::host_to_network64(5 * 8); 
            len_block[1] = core::endian::host_to_network64(ciphertext_len * 8); 

            __m128i len_v = _mm_load_si128(reinterpret_cast<const __m128i*>(len_block));
            len_v = _mm_shuffle_epi8(len_v, bswap_mask);
            ghash_state = _mm_xor_si128(ghash_state, len_v);
            ghash_state = ghash_mul(ghash_state, H);

            ghash_state = _mm_shuffle_epi8(ghash_state, bswap_mask);
            __m128i expected_tag = _mm_xor_si128(ghash_state, e_j0);

            // 4. Constant-Time Authentication
            __m128i actual_tag = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in_out_buffer + ciphertext_len));
            __m128i diff = _mm_xor_si128(expected_tag, actual_tag);

            if (SL_EXPECT_FALSE(!_mm_testz_si128(diff, diff))) {
                return 0; // Cryptographic Forgery Detected!
            }

            return ciphertext_len;
        }

        /**
         * @brief In-Place Encryption and Authentication (RFC 8446).
         * @details Encrypts the plaintext over itself and appends the 16-byte GHASH tag,
         * executing directly against the TCP egress ring buffer.
         */
        SLAB_HOT static void encrypt_in_place(
            const __m128i* round_keys,
            const uint8_t* iv,
            uint64_t sequence_number,
            char* in_out_buffer,
            size_t plaintext_length
        ) noexcept {
            // 1. Construct the TLS 1.3 Per-Record Nonce (IV XOR Sequence Number)
            alignas(16) uint8_t nonce[16] = {0};
            __builtin_memcpy(nonce, iv, 12);
            uint64_t net_seq = core::endian::network_to_host64(sequence_number);
            *reinterpret_cast<uint64_t*>(nonce + 4) ^= net_seq;
            nonce[15] = 1; // Counter block initialization

            __m128i counter = _mm_load_si128(reinterpret_cast<const __m128i*>(nonce));

            // Generate GHASH Subkey (H)
            __m128i H = _mm_setzero_si128();
            H = _mm_xor_si128(H, round_keys[0]);
            for (int i = 1; i < 10; ++i) H = _mm_aesenc_si128(H, round_keys[i]);
            H = _mm_aesenclast_si128(H, round_keys[10]);
            
            const __m128i bswap_mask = _mm_setr_epi8(15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0);
            H = _mm_shuffle_epi8(H, bswap_mask);

            // Generate initial E(K, J0) for final tag masking
            __m128i e_j0 = _mm_xor_si128(counter, round_keys[0]);
            for (int i = 1; i < 10; ++i) e_j0 = _mm_aesenc_si128(e_j0, round_keys[i]);
            e_j0 = _mm_aesenclast_si128(e_j0, round_keys[10]);

            __m128i ghash_state = _mm_setzero_si128();

            // TLS 1.3 Outer Record Header AAD (Type 23, Version 0x0303, Total Length)
            alignas(16) uint8_t aad_block[16] = {0};
            aad_block[0] = 23; 
            aad_block[1] = 3;  
            aad_block[2] = 3;  
            uint16_t record_len = static_cast<uint16_t>(plaintext_length + 16);
            aad_block[3] = static_cast<uint8_t>(record_len >> 8);
            aad_block[4] = static_cast<uint8_t>(record_len & 0xFF);

            __m128i aad_v = _mm_load_si128(reinterpret_cast<const __m128i*>(aad_block));
            aad_v = _mm_shuffle_epi8(aad_v, bswap_mask);
            ghash_state = _mm_xor_si128(ghash_state, aad_v);
            ghash_state = ghash_mul(ghash_state, H);

            // 2. Hardware AES-CTR Encryption Pipeline & GHASH Accumulation
            size_t offset = 0;
            const __m128i one = _mm_set_epi32(0, 0, 0, 1<<24); // Big-endian 1

            while (offset < plaintext_length) {
                counter = _mm_add_epi32(counter, one); // Increment CTR
                
                __m128i block = _mm_xor_si128(counter, round_keys[0]);
                for (int i = 1; i < 10; ++i) block = _mm_aesenc_si128(block, round_keys[i]);
                block = _mm_aesenclast_si128(block, round_keys[10]);
                
                size_t chunk = (plaintext_length - offset >= 16) ? 16 : (plaintext_length - offset);
                alignas(16) uint8_t temp_plain[16] = {0};
                __builtin_memcpy(temp_plain, in_out_buffer + offset, chunk);

                __m128i plain_block = _mm_load_si128(reinterpret_cast<const __m128i*>(temp_plain));
                __m128i cipher_block = _mm_xor_si128(plain_block, block);
                
                // Zero remainder out for clean GHASH padding on partial blocks
                if (chunk < 16) {
                    alignas(16) uint8_t temp_cipher[16];
                    _mm_store_si128(reinterpret_cast<__m128i*>(temp_cipher), cipher_block);
                    __builtin_memset(temp_cipher + chunk, 0, 16 - chunk);
                    cipher_block = _mm_load_si128(reinterpret_cast<const __m128i*>(temp_cipher));
                }
                
                // GHASH Accumulation
                __m128i cb_swapped = _mm_shuffle_epi8(cipher_block, bswap_mask);
                ghash_state = _mm_xor_si128(ghash_state, cb_swapped);
                ghash_state = ghash_mul(ghash_state, H);

                // Write ciphertext back safely
                __builtin_memcpy(in_out_buffer + offset, reinterpret_cast<const char*>(&cipher_block), chunk);
                offset += chunk;
            }

            // 3. Finalize GHASH (Append lengths block)
            alignas(16) uint64_t len_block[2];
            len_block[0] = core::endian::host_to_network64(5 * 8); 
            len_block[1] = core::endian::host_to_network64(plaintext_length * 8); 

            __m128i len_v = _mm_load_si128(reinterpret_cast<const __m128i*>(len_block));
            len_v = _mm_shuffle_epi8(len_v, bswap_mask);
            ghash_state = _mm_xor_si128(ghash_state, len_v);
            ghash_state = ghash_mul(ghash_state, H);

            ghash_state = _mm_shuffle_epi8(ghash_state, bswap_mask);
            __m128i tag = _mm_xor_si128(ghash_state, e_j0);

            // 4. GHASH Tag Injection
            _mm_storeu_si128(reinterpret_cast<__m128i*>(in_out_buffer + plaintext_length), tag);
        }
    };

} // namespace slabflux::security