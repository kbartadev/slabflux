/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * @file virtual_tls_socket.hpp
 * @brief Hardware-Accelerated TLS 1.3 Record Layer Wrapper (AES-128-GCM).
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <wmmintrin.h> // AES-NI & PCLMULQDQ
#include "slabflux/net/virtual_tcp_socket.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::net {

    /**
     * @brief Hardware-accelerated AES-128-GCM Cryptographic Engine.
     * @details Executes AES-CTR and GHASH using strictly L1-resident SIMD intrinsics.
     */
    class alignas(64) aes_gcm_128_engine {
        __m128i rk_[11]; // AES-128 Round Keys
        __m128i h_;      // GHASH Subkey
        uint64_t implicit_iv_[2]; 
        uint64_t sequence_number_{0};

        // Constant time AES Key Expansion
        static SLAB_FORCE_INLINE __m128i expand_key_step(__m128i key, __m128i keygened) noexcept {
            key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
            key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
            key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
            keygened = _mm_shuffle_epi32(keygened, _MM_SHUFFLE(3, 3, 3, 3));
            return _mm_xor_si128(key, keygened);
        }

        // Galois Field Multiplication (GHASH) using PCLMULQDQ
        SLAB_FORCE_INLINE __m128i gfmul(__m128i a, __m128i b) const noexcept {
            __m128i tmp3 = _mm_clmulepi64_si128(a, b, 0x00);
            __m128i tmp4 = _mm_clmulepi64_si128(a, b, 0x10);
            __m128i tmp5 = _mm_clmulepi64_si128(a, b, 0x01);
            __m128i tmp6 = _mm_clmulepi64_si128(a, b, 0x11);
            
            tmp4 = _mm_xor_si128(tmp4, tmp5);
            tmp5 = _mm_slli_si128(tmp4, 8);
            tmp4 = _mm_srli_si128(tmp4, 8);
            tmp3 = _mm_xor_si128(tmp3, tmp5);
            tmp6 = _mm_xor_si128(tmp6, tmp4);
            
            // Reduction polynomial x^128 + x^7 + x^2 + x + 1
            __m128i tmp7 = _mm_srli_epi32(tmp3, 31);
            __m128i tmp8 = _mm_srli_epi32(tmp6, 31);
            tmp3 = _mm_slli_epi32(tmp3, 1);
            tmp6 = _mm_slli_epi32(tmp6, 1);
            
            __m128i tmp9 = _mm_srli_si128(tmp7, 12);
            tmp8 = _mm_slli_si128(tmp8, 4);
            tmp7 = _mm_slli_si128(tmp7, 4);
            tmp3 = _mm_or_si128(tmp3, tmp7);
            tmp6 = _mm_or_si128(tmp6, tmp8);
            tmp6 = _mm_or_si128(tmp6, tmp9);
            
            __m128i tmp10 = _mm_slli_epi32(tmp3, 31);
            __m128i tmp11 = _mm_slli_epi32(tmp3, 30);
            __m128i tmp12 = _mm_slli_epi32(tmp3, 25);
            
            tmp10 = _mm_xor_si128(tmp10, tmp11);
            tmp10 = _mm_xor_si128(tmp10, tmp12);
            tmp8 = _mm_srli_si128(tmp10, 4);
            tmp10 = _mm_slli_si128(tmp10, 12);
            tmp3 = _mm_xor_si128(tmp3, tmp10);
            
            tmp10 = _mm_srli_epi32(tmp3, 1);
            tmp11 = _mm_srli_epi32(tmp3, 2);
            tmp12 = _mm_srli_epi32(tmp3, 7);
            tmp10 = _mm_xor_si128(tmp10, tmp11);
            tmp10 = _mm_xor_si128(tmp10, tmp12);
            tmp10 = _mm_xor_si128(tmp10, tmp8);
            tmp3 = _mm_xor_si128(tmp3, tmp10);
            
            return _mm_xor_si128(tmp6, tmp3);
        }

    public:
        aes_gcm_128_engine() = default;

        void set_key(const uint8_t key[16], const uint8_t iv[12]) noexcept {
            rk_[0] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(key));
            rk_[1] = expand_key_step(rk_[0], _mm_aeskeygenassist_si128(rk_[0], 0x01));
            rk_[2] = expand_key_step(rk_[1], _mm_aeskeygenassist_si128(rk_[1], 0x02));
            rk_[3] = expand_key_step(rk_[2], _mm_aeskeygenassist_si128(rk_[2], 0x04));
            rk_[4] = expand_key_step(rk_[3], _mm_aeskeygenassist_si128(rk_[3], 0x08));
            rk_[5] = expand_key_step(rk_[4], _mm_aeskeygenassist_si128(rk_[4], 0x10));
            rk_[6] = expand_key_step(rk_[5], _mm_aeskeygenassist_si128(rk_[5], 0x20));
            rk_[7] = expand_key_step(rk_[6], _mm_aeskeygenassist_si128(rk_[6], 0x40));
            rk_[8] = expand_key_step(rk_[7], _mm_aeskeygenassist_si128(rk_[7], 0x80));
            rk_[9] = expand_key_step(rk_[8], _mm_aeskeygenassist_si128(rk_[8], 0x1B));
            rk_[10] = expand_key_step(rk_[9], _mm_aeskeygenassist_si128(rk_[9], 0x36));

            // Precompute GHASH Subkey (H)
            __m128i zero = _mm_setzero_si128();
            zero = _mm_xor_si128(zero, rk_[0]);
            for (int i = 1; i <= 9; ++i) zero = _mm_aesenc_si128(zero, rk_[i]);
            h_ = _mm_aesenclast_si128(zero, rk_[10]);
            
            // Endian swap H for GCM reflection
            const __m128i bswap_mask = _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
            h_ = _mm_shuffle_epi8(h_, bswap_mask);

            std::memset(implicit_iv_, 0, 16);
            std::memcpy(implicit_iv_, iv, 12);
            sequence_number_ = 0;
        }

        SLAB_FORCE_INLINE void encrypt_block(const uint8_t in[16], uint8_t out[16]) const noexcept {
            __m128i block = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in));
            block = _mm_xor_si128(block, rk_[0]);
            for (int i = 1; i <= 9; ++i) block = _mm_aesenc_si128(block, rk_[i]);
            block = _mm_aesenclast_si128(block, rk_[10]);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out), block);
        }

        SLAB_HOT void encrypt_record(const char* plaintext, size_t pt_len, char* ciphertext, uint8_t inner_type) noexcept {
            // Explicit Nonce Construction: Implicit IV XORed with Sequence Number
            uint64_t nonce[2] = {implicit_iv_[0], implicit_iv_[1]};
            nonce[1] ^= __builtin_bswap64(sequence_number_++);
            
            // CTR Initial Counter Block (J0)
            alignas(16) uint8_t cb[16];
            std::memcpy(cb, nonce, 12);
            *reinterpret_cast<uint32_t*>(&cb[12]) = __builtin_bswap32(1); // Counter starts at 1
            __m128i counter = _mm_load_si128(reinterpret_cast<__m128i*>(cb));

            // Precompute E(K, J0) for the Auth Tag
            __m128i tag_mask = counter;
            tag_mask = _mm_xor_si128(tag_mask, rk_[0]);
            for (int i = 1; i <= 9; ++i) tag_mask = _mm_aesenc_si128(tag_mask, rk_[i]);
            tag_mask = _mm_aesenclast_si128(tag_mask, rk_[10]);

            const __m128i bswap_mask = _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
            __m128i ghash_state = _mm_setzero_si128();
            
            // 1. GHASH AAD (TLS 1.3 Record Header)
            alignas(16) uint8_t aad[16] = {0x17, 0x03, 0x03, static_cast<uint8_t>((pt_len + 17) >> 8), static_cast<uint8_t>((pt_len + 17) & 0xFF)};
            __m128i aad_block = _mm_load_si128(reinterpret_cast<__m128i*>(aad));
            aad_block = _mm_shuffle_epi8(aad_block, bswap_mask);
            ghash_state = gfmul(_mm_xor_si128(ghash_state, aad_block), h_);

            // 2. Encrypt Payload + Inner Type using AES-CTR and update GHASH
            size_t blocks = (pt_len + 1) / 16;
            size_t remainder = (pt_len + 1) % 16;
            uint32_t c32 = 1;
            
            alignas(16) uint8_t tmp[16];
            for (size_t i = 0; i < blocks; ++i) {
                c32++;
                *reinterpret_cast<uint32_t*>(&cb[12]) = __builtin_bswap32(c32);
                counter = _mm_load_si128(reinterpret_cast<__m128i*>(cb));
                // AES Encrypt Counter
                __m128i e_ctr = _mm_xor_si128(counter, rk_[0]);
                for (int r = 1; r <= 9; ++r) e_ctr = _mm_aesenc_si128(e_ctr, rk_[r]);
                e_ctr = _mm_aesenclast_si128(e_ctr, rk_[10]);
                
                if (i == blocks - 1 && remainder == 0) {
                    std::memcpy(tmp, plaintext + i * 16, 15);
                    tmp[15] = inner_type;
                } else {
                    std::memcpy(tmp, plaintext + i * 16, 16);
                }
                __m128i pt = _mm_load_si128(reinterpret_cast<__m128i*>(tmp));
                __m128i ct = _mm_xor_si128(pt, e_ctr);
                _mm_storeu_si128(reinterpret_cast<__m128i*>(ciphertext + i * 16), ct);
                
                ghash_state = gfmul(_mm_xor_si128(ghash_state, _mm_shuffle_epi8(ct, bswap_mask)), h_);
            }

            // Handle remainder
            if (remainder > 0) {
                c32++;
                *reinterpret_cast<uint32_t*>(&cb[12]) = __builtin_bswap32(c32);
                counter = _mm_load_si128(reinterpret_cast<__m128i*>(cb));
                __m128i e_ctr = _mm_xor_si128(counter, rk_[0]);
                for (int r = 1; r <= 9; ++r) e_ctr = _mm_aesenc_si128(e_ctr, rk_[r]);
                e_ctr = _mm_aesenclast_si128(e_ctr, rk_[10]);
                
                std::memset(tmp, 0, 16);
                std::memcpy(tmp, plaintext + blocks * 16, pt_len - blocks * 16);
                tmp[pt_len - blocks * 16] = inner_type;
                
                __m128i pt = _mm_load_si128(reinterpret_cast<__m128i*>(tmp));
                __m128i ct = _mm_xor_si128(pt, e_ctr);
                std::memcpy(ciphertext + blocks * 16, &ct, remainder);
                
                // Pad GHASH block with zeros
                std::memset(tmp, 0, 16);
                std::memcpy(tmp, &ct, remainder);
                __m128i ct_padded = _mm_load_si128(reinterpret_cast<__m128i*>(tmp));
                ghash_state = gfmul(_mm_xor_si128(ghash_state, _mm_shuffle_epi8(ct_padded, bswap_mask)), h_);
            }

            // 3. Finalize GHASH with lengths and output Auth Tag
            __m128i lengths = _mm_set_epi32(0, (pt_len + 1) * 8, 0, 5 * 8); // Bits!
            ghash_state = gfmul(_mm_xor_si128(ghash_state, lengths), h_);
            ghash_state = _mm_shuffle_epi8(ghash_state, bswap_mask);
            
            __m128i tag = _mm_xor_si128(ghash_state, tag_mask);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(ciphertext + pt_len + 1), tag);
        }

        SLAB_HOT bool decrypt_quic(const uint8_t* ciphertext, size_t ct_len, uint8_t* plaintext, 
                                   const uint8_t* aad, size_t aad_len, uint64_t packet_number) noexcept {
            if (SL_EXPECT_FALSE(ct_len < 16)) return false;
            size_t pt_len = ct_len - 16;
            
            alignas(16) uint8_t cb[16] = {0};
            std::memcpy(cb, implicit_iv_, 12);
            uint64_t pn_net = __builtin_bswap64(packet_number);
            const uint8_t* pn_bytes = reinterpret_cast<const uint8_t*>(&pn_net);
            for(int i = 0; i < 8; ++i) cb[4 + i] ^= pn_bytes[i];
            
            *reinterpret_cast<uint32_t*>(&cb[12]) = __builtin_bswap32(1);
            __m128i counter = _mm_load_si128(reinterpret_cast<__m128i*>(cb));

            __m128i tag_mask = counter;
            tag_mask = _mm_xor_si128(tag_mask, rk_[0]);
            for (int i = 1; i <= 9; ++i) tag_mask = _mm_aesenc_si128(tag_mask, rk_[i]);
            tag_mask = _mm_aesenclast_si128(tag_mask, rk_[10]);

            const __m128i bswap_mask = _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
            __m128i ghash_state = _mm_setzero_si128();

            size_t aad_blocks = aad_len / 16;
            size_t aad_rem = aad_len % 16;
            for (size_t i = 0; i < aad_blocks; ++i) {
                __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(aad + i * 16));
                ghash_state = gfmul(_mm_xor_si128(ghash_state, _mm_shuffle_epi8(a, bswap_mask)), h_);
            }
            if (aad_rem > 0) {
                alignas(16) uint8_t tmp[16] = {0};
                std::memcpy(tmp, aad + aad_blocks * 16, aad_rem);
                __m128i a = _mm_load_si128(reinterpret_cast<__m128i*>(tmp));
                ghash_state = gfmul(_mm_xor_si128(ghash_state, _mm_shuffle_epi8(a, bswap_mask)), h_);
            }

            size_t blocks = pt_len / 16;
            size_t remainder = pt_len % 16;
            uint32_t c32 = 1;

            for (size_t i = 0; i < blocks; ++i) {
                __m128i ct = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ciphertext + i * 16));
                ghash_state = gfmul(_mm_xor_si128(ghash_state, _mm_shuffle_epi8(ct, bswap_mask)), h_);
                
                c32++;
                *reinterpret_cast<uint32_t*>(&cb[12]) = __builtin_bswap32(c32);
                __m128i e_ctr = _mm_load_si128(reinterpret_cast<__m128i*>(cb));
                e_ctr = _mm_xor_si128(e_ctr, rk_[0]);
                for (int r = 1; r <= 9; ++r) e_ctr = _mm_aesenc_si128(e_ctr, rk_[r]);
                e_ctr = _mm_aesenclast_si128(e_ctr, rk_[10]);
                
                __m128i pt = _mm_xor_si128(ct, e_ctr);
                _mm_storeu_si128(reinterpret_cast<__m128i*>(plaintext + i * 16), pt);
            }

            if (remainder > 0) {
                alignas(16) uint8_t tmp[16] = {0};
                std::memcpy(tmp, ciphertext + blocks * 16, remainder);
                __m128i ct = _mm_load_si128(reinterpret_cast<__m128i*>(tmp));
                ghash_state = gfmul(_mm_xor_si128(ghash_state, _mm_shuffle_epi8(ct, bswap_mask)), h_);
                
                c32++;
                *reinterpret_cast<uint32_t*>(&cb[12]) = __builtin_bswap32(c32);
                __m128i e_ctr = _mm_load_si128(reinterpret_cast<__m128i*>(cb));
                e_ctr = _mm_xor_si128(e_ctr, rk_[0]);
                for (int r = 1; r <= 9; ++r) e_ctr = _mm_aesenc_si128(e_ctr, rk_[r]);
                e_ctr = _mm_aesenclast_si128(e_ctr, rk_[10]);
                
                __m128i pt = _mm_xor_si128(ct, e_ctr);
                std::memcpy(plaintext + blocks * 16, &pt, remainder);
            }

            __m128i lengths = _mm_set_epi32(0, pt_len * 8, 0, aad_len * 8);
            ghash_state = gfmul(_mm_xor_si128(ghash_state, lengths), h_);
            ghash_state = _mm_shuffle_epi8(ghash_state, bswap_mask);
            
            __m128i tag = _mm_xor_si128(ghash_state, tag_mask);
            
            alignas(16) uint8_t calc_tag[16];
            _mm_store_si128(reinterpret_cast<__m128i*>(calc_tag), tag);
            
            uint32_t diff = 0;
            for(int i = 0; i < 16; ++i) diff |= calc_tag[i] ^ ciphertext[pt_len + i];
            
            return diff == 0;
        }

        /**
         * @brief Natively encrypts a QUIC packet payload with AES-128-GCM.
         * @details Injects the QUIC packet header as Additional Authenticated Data (AAD).
         */
        SLAB_HOT void encrypt_quic(const uint8_t* plaintext, size_t pt_len, uint8_t* ciphertext, 
                                   const uint8_t* aad, size_t aad_len, uint64_t packet_number) noexcept {
            alignas(16) uint8_t cb[16] = {0};
            std::memcpy(cb, implicit_iv_, 12);
            uint64_t pn_net = __builtin_bswap64(packet_number);
            const uint8_t* pn_bytes = reinterpret_cast<const uint8_t*>(&pn_net);
            for(int i = 0; i < 8; ++i) cb[4 + i] ^= pn_bytes[i];
            
            *reinterpret_cast<uint32_t*>(&cb[12]) = __builtin_bswap32(1);
            __m128i counter = _mm_load_si128(reinterpret_cast<__m128i*>(cb));

            __m128i tag_mask = counter;
            tag_mask = _mm_xor_si128(tag_mask, rk_[0]);
            for (int i = 1; i <= 9; ++i) tag_mask = _mm_aesenc_si128(tag_mask, rk_[i]);
            tag_mask = _mm_aesenclast_si128(tag_mask, rk_[10]);

            const __m128i bswap_mask = _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
            __m128i ghash_state = _mm_setzero_si128();

            size_t aad_blocks = aad_len / 16;
            size_t aad_rem = aad_len % 16;
            for (size_t i = 0; i < aad_blocks; ++i) {
                __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(aad + i * 16));
                ghash_state = gfmul(_mm_xor_si128(ghash_state, _mm_shuffle_epi8(a, bswap_mask)), h_);
            }
            if (aad_rem > 0) {
                alignas(16) uint8_t tmp[16] = {0};
                std::memcpy(tmp, aad + aad_blocks * 16, aad_rem);
                __m128i a = _mm_load_si128(reinterpret_cast<__m128i*>(tmp));
                ghash_state = gfmul(_mm_xor_si128(ghash_state, _mm_shuffle_epi8(a, bswap_mask)), h_);
            }

            size_t blocks = pt_len / 16;
            size_t remainder = pt_len % 16;
            uint32_t c32 = 1;

            for (size_t i = 0; i < blocks; ++i) {
                c32++;
                *reinterpret_cast<uint32_t*>(&cb[12]) = __builtin_bswap32(c32);
                __m128i e_ctr = _mm_load_si128(reinterpret_cast<__m128i*>(cb));
                e_ctr = _mm_xor_si128(e_ctr, rk_[0]);
                for (int r = 1; r <= 9; ++r) e_ctr = _mm_aesenc_si128(e_ctr, rk_[r]);
                e_ctr = _mm_aesenclast_si128(e_ctr, rk_[10]);
                
                __m128i pt = _mm_loadu_si128(reinterpret_cast<const __m128i*>(plaintext + i * 16));
                __m128i ct = _mm_xor_si128(pt, e_ctr);
                _mm_storeu_si128(reinterpret_cast<__m128i*>(ciphertext + i * 16), ct);
                ghash_state = gfmul(_mm_xor_si128(ghash_state, _mm_shuffle_epi8(ct, bswap_mask)), h_);
            }

            if (remainder > 0) {
                alignas(16) uint8_t tmp[16] = {0};
                std::memcpy(tmp, plaintext + blocks * 16, remainder);
                __m128i pt = _mm_load_si128(reinterpret_cast<__m128i*>(tmp));
                
                c32++;
                *reinterpret_cast<uint32_t*>(&cb[12]) = __builtin_bswap32(c32);
                __m128i e_ctr = _mm_load_si128(reinterpret_cast<__m128i*>(cb));
                e_ctr = _mm_xor_si128(e_ctr, rk_[0]);
                for (int r = 1; r <= 9; ++r) e_ctr = _mm_aesenc_si128(e_ctr, rk_[r]);
                e_ctr = _mm_aesenclast_si128(e_ctr, rk_[10]);
                
                __m128i ct = _mm_xor_si128(pt, e_ctr);
                std::memcpy(ciphertext + blocks * 16, &ct, remainder);
                
                std::memset(tmp, 0, 16);
                std::memcpy(tmp, &ct, remainder);
                __m128i ct_padded = _mm_load_si128(reinterpret_cast<__m128i*>(tmp));
                ghash_state = gfmul(_mm_xor_si128(ghash_state, _mm_shuffle_epi8(ct_padded, bswap_mask)), h_);
            }

            __m128i lengths = _mm_set_epi32(0, pt_len * 8, 0, aad_len * 8);
            ghash_state = gfmul(_mm_xor_si128(ghash_state, lengths), h_);
            ghash_state = _mm_shuffle_epi8(ghash_state, bswap_mask);
            
            __m128i tag = _mm_xor_si128(ghash_state, tag_mask);
            
            alignas(16) uint8_t calc_tag[16];
            _mm_store_si128(reinterpret_cast<__m128i*>(calc_tag), tag);
            std::memcpy(ciphertext + pt_len, calc_tag, 16);
        }
    };

    /**
     * @brief Zero-Allocation Virtual TLS 1.3 Socket.
     * @details Transparently wraps the underlying virtual_tcp_socket, providing 
     * symmetrical API functionality with inline AES-128-GCM record protection.
     */
    class virtual_tls_socket {
        virtual_tcp_socket tcp_;
        aes_gcm_128_engine tx_engine_;
        aes_gcm_128_engine rx_engine_;

    public:
        explicit virtual_tls_socket(virtual_tcp_socket tcp) noexcept : tcp_(tcp) {}

        void set_traffic_keys(const uint8_t client_key[16], const uint8_t client_iv[12], const uint8_t server_key[16], const uint8_t server_iv[12], bool is_server) noexcept {
            if (is_server) {
                tx_engine_.set_key(server_key, server_iv);
                rx_engine_.set_key(client_key, client_iv);
            } else {
                tx_engine_.set_key(client_key, client_iv);
                rx_engine_.set_key(server_key, server_iv);
            }
        }

        [[nodiscard]] SLAB_FORCE_INLINE bool is_valid() const noexcept { return tcp_.is_valid(); }
        [[nodiscard]] SLAB_FORCE_INLINE bool is_established() const noexcept { return tcp_.is_established(); }

        SLAB_HOT ssize_t send(const char* buffer, size_t length) noexcept {
            if (SL_EXPECT_FALSE(!is_established() || length > 1400)) return -1;

            // Local L1-aligned buffer to prevent heap allocation during encryption
            alignas(64) char tls_record[1500];
            
            // TLS 1.3 Application Data Header
            tls_record[0] = 0x17;
            tls_record[1] = 0x03; tls_record[2] = 0x03;
            uint16_t cipher_len = length + 1 + 16; // Payload + InnerType + Tag
            tls_record[3] = static_cast<uint8_t>(cipher_len >> 8);
            tls_record[4] = static_cast<uint8_t>(cipher_len & 0xFF);

            tx_engine_.encrypt_record(buffer, length, tls_record + 5, 0x17);
            
            return tcp_.send(tls_record, 5 + cipher_len);
        }

        inline void close() noexcept { tcp_.close(); }
    };

} // namespace slabflux::net