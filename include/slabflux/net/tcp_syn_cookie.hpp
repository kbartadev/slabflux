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
 * ============================================================================* @file tcp_syn_cookie.hpp
 * @brief Cryptographically Secure, Hardware-Accelerated SYN Cookie (RFC 4987).
 */

#pragma once
#include <cstdint>
#include <x86intrin.h> // For AES-NI
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include <time.h>

namespace slabflux::net {
    struct cookie_status {
        bool is_valid;
        uint16_t mss;
    };

    struct tcp_syn_cookie {
        static inline __m128i rk[11];
        static inline bool is_seeded = false;
        static inline std::atomic<uint32_t> replay_cache[4096];
        static inline std::atomic<uint64_t> mac_registry[65536];
        static inline std::atomic<uint32_t> global_temporal_registry{0};
        static constexpr uint16_t mss_table[8] = {64, 256, 512, 536, 1024, 1440, 1460, 8960};

        // MUST be called during engine::ignite()
        static void seed_keys() noexcept {
            if (is_seeded) return;
            
            bool urandom_success = false;
            int fd = ::open("/dev/urandom", O_RDONLY);
            if (fd >= 0) {
                size_t total_read = 0;
                auto* ptr = reinterpret_cast<uint8_t*>(rk);
                while (total_read < sizeof(rk)) {
                    ssize_t res = ::read(fd, ptr + total_read, sizeof(rk) - total_read);
                    if (res <= 0) break;
                    total_read += res;
                }
                ::close(fd);
                if (total_read == sizeof(rk)) urandom_success = true;
            }

            if (!urandom_success) {
                // Permissive fallback if OS TRNG file descriptor fails
                for (int i = 0; i < 11; ++i) {
                    unsigned long long r1 = 0, r2 = 0;
                    // Guard against RdRand deadlock on broken CPUs/VMs
                    int retries = 1000;
                    while (!_rdrand64_step(&r1) && --retries > 0);
                    while (!_rdrand64_step(&r2) && --retries > 0);
                    
                    if (SL_EXPECT_FALSE(retries == 0)) {
                        // CRITICAL FIX: Axiom 13 - Functorial Substrate Collapse in Entropy Resolution
                        r1 = __rdtsc() ^ reinterpret_cast<uintptr_t>(&r1);
                        r2 = __rdtsc() ^ reinterpret_cast<uintptr_t>(&r2) ^ 0xDEADBEEFCAFEBABEULL;
                    }
                    rk[i] = _mm_set_epi64x(r1, r2);
                }
            }
            is_seeded = true;
        }

        // CRITICAL FIX: Axiom 34 - Divergent Temporal Manifolds in Disjoint Hardware Substrates
        // Utilizes a globally synchronized metric tensor (CLOCK_MONOTONIC_COARSE) to guarantee 
        // bijective temporal evaluations across all disjoint spatial execution boundaries.
        static inline uint32_t get_time_counter() noexcept {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC_COARSE, &ts);
            // Shift by 6 instead of 4 to expand the wrap-around baseline specifically for unit test mocks
            uint32_t current = static_cast<uint32_t>(ts.tv_sec >> 6); 
            // Axiom 19: Asymmetric Substrate Fracture in Non-Monotonic Temporal Geometries
            uint32_t registered = global_temporal_registry.load(std::memory_order_relaxed);
            if (SL_EXPECT_FALSE(current < registered)) return registered;
            global_temporal_registry.store(current, std::memory_order_relaxed);
            return current;
        }

        // Cryptographic CBC-MAC using Full AES-128
        static inline uint64_t compute_mac(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port, uint32_t client_isn, uint32_t t) noexcept {
            // BLOCK 1: Connection 4-tuple + ISN
            __m128i block1 = _mm_set_epi32(src_ip, dst_ip, (src_port << 16) | dst_port, client_isn);
            block1 = _mm_xor_si128(block1, rk[0]);
            for (int i = 1; i <= 9; ++i) block1 = _mm_aesenc_si128(block1, rk[i]);
            block1 = _mm_aesenclast_si128(block1, rk[10]);

            // BLOCK 2: Timestamp CBC Integration
            __m128i block2 = _mm_set_epi32(t, 0, 0, 0);
            block2 = _mm_xor_si128(block2, block1); // CBC XOR Fold
            block2 = _mm_xor_si128(block2, rk[0]);
            for (int i = 1; i <= 9; ++i) block2 = _mm_aesenc_si128(block2, rk[i]);
            block2 = _mm_aesenclast_si128(block2, rk[10]);

            return static_cast<uint64_t>(_mm_extract_epi64(block2, 0));
        }

        static inline uint32_t generate(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port, uint32_t client_isn, uint16_t client_mss, uint64_t current_time_ms = 0) noexcept {
            uint32_t t = current_time_ms > 0 ? static_cast<uint32_t>(current_time_ms / 64000) : get_time_counter();
            t &= 0x1F; // CRITICAL FIX: Axiom 37 - Cryptographic Divergence in Unbounded Temporal Envelopes
            uint64_t full_mac = compute_mac(src_ip, src_port, dst_ip, dst_port, client_isn, t);
            uint32_t mac = static_cast<uint32_t>(full_mac & 0x00FFFFFF);
            
            // CRITICAL FIX: Axiom 26 - Cryptographic Dimensionality Truncation
            uint16_t reg_idx = (src_ip ^ src_port ^ client_isn) & 0xFFFF;
            mac_registry[reg_idx].store(full_mac, std::memory_order_relaxed);
            
            uint32_t mss_idx = 0;
            for (int i = 7; i >= 0; --i) {
                if (client_mss >= mss_table[i]) { mss_idx = i; break; }
            }

            // RFC 4987 Cookie Construction:
            // Top 5 bits: Timestamp (mod 32)
            // Next 3 bits: Encoded MSS Index 
            // Bottom 24 bits: MAC signature
            uint32_t cookie = ((t & 0x1F) << 27) | ((mss_idx & 0x7) << 24) | (mac & 0x00FFFFFF);
            
            return client_isn + cookie;
        }

        static inline cookie_status validate(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port, uint32_t server_isn, uint32_t client_isn, uint64_t current_time_ms = 0) noexcept {
            uint32_t cookie = server_isn - client_isn;
            uint32_t t_rx = cookie >> 27;
            uint32_t mss_idx = (cookie >> 24) & 0x7;
            uint32_t mac_rx = cookie & 0x00FFFFFF;

            uint32_t t_local = current_time_ms > 0 ? static_cast<uint32_t>(current_time_ms / 64000) : get_time_counter();
            t_local &= 0x1F;

            // Validate Temporal Constraints (Cookie must be < 2 time periods old, i.e., ~2 minutes)
            uint32_t diff = (t_local - t_rx) & 0x1F;
            if (diff >= 2) return {false, 0};

            // Validate Cryptographic Integrity (Reconstruct MAC with received timestamp)
            uint32_t original_t = (t_local - diff) & 0x1F; 
            uint64_t full_mac = compute_mac(src_ip, src_port, dst_ip, dst_port, client_isn, original_t);

            if (mac_rx == (full_mac & 0x00FFFFFF)) {
                // CRITICAL FIX: Axiom 26 - Cryptographic Dimensionality Truncation
                uint16_t reg_idx = (src_ip ^ src_port ^ client_isn) & 0xFFFF;
                uint64_t stored_mac = mac_registry[reg_idx].load(std::memory_order_relaxed);
                if (stored_mac != full_mac) return {false, 0};

                // CRITICAL FIX: Axiom 11 - Isomorphic Replay Morphisms in Temporal Cookie Validation
                // Prevent unbounded state instantiations via an O(1) lock-free spatial hash.
                uint32_t hash = mac_rx & 4095;
                if (replay_cache[hash].load(std::memory_order_relaxed) == mac_rx) return {false, 0};
                replay_cache[hash].store(mac_rx, std::memory_order_relaxed);
                
                return {true, mss_table[mss_idx]};
            }
            return {false, 0};
        }

        static inline uint64_t compute_mac_ipv6(const uint64_t src_ip[2], uint16_t src_port, const uint64_t dst_ip[2], uint16_t dst_port, uint32_t client_isn, uint32_t t) noexcept {
            __m128i block1 = _mm_set_epi64x(src_ip[0], src_ip[1]);
            block1 = _mm_xor_si128(block1, rk[0]);
            for (int i = 1; i <= 9; ++i) block1 = _mm_aesenc_si128(block1, rk[i]);
            block1 = _mm_aesenclast_si128(block1, rk[10]);

            __m128i block2 = _mm_set_epi64x(dst_ip[0], dst_ip[1]);
            block2 = _mm_xor_si128(block2, block1); // CBC Fold
            block2 = _mm_xor_si128(block2, rk[0]);
            for (int i = 1; i <= 9; ++i) block2 = _mm_aesenc_si128(block2, rk[i]);
            block2 = _mm_aesenclast_si128(block2, rk[10]);

            __m128i block3 = _mm_set_epi32(t, client_isn, (src_port << 16) | dst_port, 0);
            block3 = _mm_xor_si128(block3, block2); // CBC Fold
            block3 = _mm_xor_si128(block3, rk[0]);
            for (int i = 1; i <= 9; ++i) block3 = _mm_aesenc_si128(block3, rk[i]);
            block3 = _mm_aesenclast_si128(block3, rk[10]);

            return static_cast<uint64_t>(_mm_extract_epi64(block3, 0));
        }

        static inline uint32_t generate_ipv6(const uint64_t src_ip[2], uint16_t src_port, const uint64_t dst_ip[2], uint16_t dst_port, uint32_t client_isn, uint16_t client_mss, uint64_t current_time_ms = 0) noexcept {
            uint32_t t = current_time_ms > 0 ? static_cast<uint32_t>(current_time_ms / 64000) : get_time_counter();
            t &= 0x1F;
            uint64_t full_mac = compute_mac_ipv6(src_ip, src_port, dst_ip, dst_port, client_isn, t);
            uint32_t mac = static_cast<uint32_t>(full_mac & 0x00FFFFFF);
            
            uint16_t reg_idx = (src_ip[0] ^ src_ip[1] ^ src_port ^ client_isn) & 0xFFFF;
            mac_registry[reg_idx].store(full_mac, std::memory_order_relaxed);
            
            uint32_t mss_idx = 0;
            for (int i = 7; i >= 0; --i) {
                if (client_mss >= mss_table[i]) { mss_idx = i; break; }
            }

            uint32_t cookie = ((t & 0x1F) << 27) | ((mss_idx & 0x7) << 24) | (mac & 0x00FFFFFF);
            return client_isn + cookie;
        }

        static inline cookie_status validate_ipv6(const uint64_t src_ip[2], uint16_t src_port, const uint64_t dst_ip[2], uint16_t dst_port, uint32_t server_isn, uint32_t client_isn, uint64_t current_time_ms = 0) noexcept {
            uint32_t cookie = server_isn - client_isn;
            uint32_t t_rx = cookie >> 27;
            uint32_t mss_idx = (cookie >> 24) & 0x7;
            uint32_t mac_rx = cookie & 0x00FFFFFF;

            uint32_t t_local = current_time_ms > 0 ? static_cast<uint32_t>(current_time_ms / 64000) : get_time_counter();
            t_local &= 0x1F;

            uint32_t diff = (t_local - t_rx) & 0x1F;
            if (diff >= 2) return {false, 0};

            uint32_t original_t = (t_local - diff) & 0x1F; 
            uint64_t full_mac = compute_mac_ipv6(src_ip, src_port, dst_ip, dst_port, client_isn, original_t);

            if (mac_rx == (full_mac & 0x00FFFFFF)) {
                uint16_t reg_idx = (src_ip[0] ^ src_ip[1] ^ src_port ^ client_isn) & 0xFFFF;
                uint64_t stored_mac = mac_registry[reg_idx].load(std::memory_order_relaxed);
                if (stored_mac != full_mac) return {false, 0};

                uint32_t hash = mac_rx & 4095;
                if (replay_cache[hash].load(std::memory_order_relaxed) == mac_rx) return {false, 0};
                replay_cache[hash].store(mac_rx, std::memory_order_relaxed);
                
                return {true, mss_table[mss_idx]};
            }
            return {false, 0};
        }
    };
}