/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * @file hkdf_sha256.hpp
 * @brief Zero-Allocation HKDF and HMAC-SHA256 Engine for TLS 1.3 / QUIC.
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::net {

    class alignas(64) sha256_engine {
        uint32_t state[8];
        uint8_t buffer[64];
        uint32_t datalen;
        uint64_t bitlen;

        static constexpr uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        static inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
        static inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (~x & z); }
        static inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) { return (x & y) ^ (x & z) ^ (y & z); }
        static inline uint32_t sig0(uint32_t x) { return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22); }
        static inline uint32_t sig1(uint32_t x) { return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25); }
        static inline uint32_t ep0(uint32_t x) { return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3); }
        static inline uint32_t ep1(uint32_t x) { return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10); }

        void transform() {
            uint32_t m[64];
            for (int i = 0; i < 16; ++i) {
                m[i] = (buffer[i * 4] << 24) | (buffer[i * 4 + 1] << 16) | (buffer[i * 4 + 2] << 8) | buffer[i * 4 + 3];
            }
            for (int i = 16; i < 64; ++i) {
                m[i] = ep1(m[i - 2]) + m[i - 7] + ep0(m[i - 15]) + m[i - 16];
            }

            uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6], h = state[7];
            for (int i = 0; i < 64; ++i) {
                uint32_t t1 = h + sig1(e) + ch(e, f, g) + k[i] + m[i];
                uint32_t t2 = sig0(a) + maj(a, b, c);
                h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
            }
            state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e; state[5] += f; state[6] += g; state[7] += h;
        }

    public:
        void init() {
            state[0] = 0x6a09e667; state[1] = 0xbb67ae85; state[2] = 0x3c6ef372; state[3] = 0xa54ff53a;
            state[4] = 0x510e527f; state[5] = 0x9b05688c; state[6] = 0x1f83d9ab; state[7] = 0x5be0cd19;
            datalen = 0; bitlen = 0;
        }

        void update(const uint8_t* data, size_t len) {
            for (size_t i = 0; i < len; ++i) {
                buffer[datalen] = data[i];
                datalen++;
                if (datalen == 64) { transform(); bitlen += 512; datalen = 0; }
            }
        }

        void finalize(uint8_t hash[32]) {
            uint32_t i = datalen;
            if (datalen < 56) {
                buffer[i++] = 0x80;
                while (i < 56) buffer[i++] = 0x00;
            } else {
                buffer[i++] = 0x80;
                while (i < 64) buffer[i++] = 0x00;
                transform();
                std::memset(buffer, 0, 56);
            }
            bitlen += datalen * 8;
            buffer[63] = bitlen; buffer[62] = bitlen >> 8; buffer[61] = bitlen >> 16; buffer[60] = bitlen >> 24;
            buffer[59] = bitlen >> 32; buffer[58] = bitlen >> 40; buffer[57] = bitlen >> 48; buffer[56] = bitlen >> 56;
            transform();
            for (i = 0; i < 4; ++i) {
                hash[i]      = (state[0] >> (24 - i * 8)) & 0xff; hash[i + 4]  = (state[1] >> (24 - i * 8)) & 0xff;
                hash[i + 8]  = (state[2] >> (24 - i * 8)) & 0xff; hash[i + 12] = (state[3] >> (24 - i * 8)) & 0xff;
                hash[i + 16] = (state[4] >> (24 - i * 8)) & 0xff; hash[i + 20] = (state[5] >> (24 - i * 8)) & 0xff;
                hash[i + 24] = (state[6] >> (24 - i * 8)) & 0xff; hash[i + 28] = (state[7] >> (24 - i * 8)) & 0xff;
            }
        }
    };

    class hkdf_sha256 {
    public:
        static void hmac(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len, uint8_t out[32]) {
            sha256_engine sha; uint8_t k[64] = {0};
            if (key_len > 64) { sha.init(); sha.update(key, key_len); sha.finalize(k); } 
            else { std::memcpy(k, key, key_len); }

            uint8_t k_ipad[64], k_opad[64];
            for (int i = 0; i < 64; ++i) { k_ipad[i] = k[i] ^ 0x36; k_opad[i] = k[i] ^ 0x5c; }

            uint8_t inner_hash[32];
            sha.init(); sha.update(k_ipad, 64); sha.update(data, data_len); sha.finalize(inner_hash);
            sha.init(); sha.update(k_opad, 64); sha.update(inner_hash, 32); sha.finalize(out);
        }

        static void extract(const uint8_t* salt, size_t salt_len, const uint8_t* ikm, size_t ikm_len, uint8_t prk[32]) {
            if (salt_len == 0) { uint8_t empty_salt[32] = {0}; hmac(empty_salt, 32, ikm, ikm_len, prk); } 
            else { hmac(salt, salt_len, ikm, ikm_len, prk); }
        }

        static void expand(const uint8_t prk[32], const uint8_t* info, size_t info_len, uint8_t* okm, size_t L) {
            uint8_t t[32]; uint8_t counter = 1; size_t okm_len = 0; sha256_engine sha;
            uint8_t k_ipad[64], k_opad[64];
            for (int i = 0; i < 32; ++i) { k_ipad[i] = prk[i] ^ 0x36; k_opad[i] = prk[i] ^ 0x5c; }
            for (int i = 32; i < 64; ++i) { k_ipad[i] = 0x36; k_opad[i] = 0x5c; }
            while (okm_len < L) {
                sha.init(); sha.update(k_ipad, 64); if (okm_len > 0) sha.update(t, 32);
                sha.update(info, info_len); sha.update(&counter, 1); uint8_t inner_hash[32]; sha.finalize(inner_hash);
                sha.init(); sha.update(k_opad, 64); sha.update(inner_hash, 32); sha.finalize(t);
                size_t copy_len = std::min<size_t>(32, L - okm_len); std::memcpy(okm + okm_len, t, copy_len);
                okm_len += copy_len; counter++;
            }
        }

        static void expand_label(const uint8_t prk[32], const char* label, const uint8_t* context, size_t context_len, uint8_t* okm, size_t L) {
            uint8_t hkdf_label[512]; size_t label_len = std::strlen(label);
            hkdf_label[0] = (L >> 8) & 0xFF; hkdf_label[1] = L & 0xFF; hkdf_label[2] = 6 + label_len;
            std::memcpy(&hkdf_label[3], "tls13 ", 6); std::memcpy(&hkdf_label[9], label, label_len);
            size_t offset = 3 + 6 + label_len; hkdf_label[offset] = context_len & 0xFF; offset++;
            if (context_len > 0) { std::memcpy(&hkdf_label[offset], context, context_len); offset += context_len; }
            expand(prk, hkdf_label, offset, okm, L);
        }
    };
} // namespace slabflux::net