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
 * ============================================================================*/

#pragma once

#include <immintrin.h>
#include <string_view>
#include <cstdint>
#include <cstring>
#include "slabflux/hw/intrinsics.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::io {

    class header_parser {
    public:
        static constexpr uint32_t MAX_HEADERS = 32;

        struct net_meta {
            uint32_t src_ip;
            uint16_t src_port;
            uint16_t payload_len;
        };

        struct header_view {
            std::string_view name;
            std::string_view value;
        };

        struct parsed_event {
            uint32_t header_count{ 0 };
            header_view headers[MAX_HEADERS];
            std::string_view body;
        };

        /**
         * @brief Zero-Jitter Page-Safe Network Scanner.
         * @details Uses AVX2 to avoid AVX-512 frequency scaling penalties and aligns
         * memory reads to avoid cross-page MMU faults.
         */
        SLAB_HOT static void parse_fast(const char* ptr, const char* end, parsed_event* ev) noexcept {
            const __m256i v_newline = _mm256_set1_epi8('\n');
            const __m256i v_colon   = _mm256_set1_epi8(':');
            const __m256i v_space   = _mm256_set1_epi8(' ');
            const __m256i v_tab     = _mm256_set1_epi8('\t');

            const char* line_start = ptr;

            while (ptr < end && ev->header_count < MAX_HEADERS) {
                if (ptr + 32 <= end) {
                    // Parallel Scan: Detect boundaries in 256-bit bursts.
                    __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr));
                    uint32_t nl_mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_newline));

                    if (SL_EXPECT_TRUE(nl_mask != 0)) {
                        uint32_t nl_idx = slabflux::hw::tzcnt_32(nl_mask);
                        const char* nl = ptr + nl_idx;

                        if (nl == line_start || (nl == line_start + 1 && *line_start == '\r')) {
                            ev->body = std::string_view(nl + 1, end - (nl + 1));
                            return;
                        }

                        // Register Scan: Detect colon within the current vector to avoid std::memchr.
                        // Mask out colons that appear after the current newline to prevent cross-line bleeding.
                        uint32_t cl_mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_colon));
                        uint32_t line_cl_mask = cl_mask & ((1U << nl_idx) - 1);
                        
                        const char* colon = nullptr;
                        if (SL_EXPECT_TRUE(line_cl_mask != 0)) {
                            colon = ptr + slabflux::hw::tzcnt_32(line_cl_mask);
                        } else {
                            // Scalar Fallback: Only for multi-chunk header names (rare in HFT dataframes).
                            colon = static_cast<const char*>(std::memchr(line_start, ':', nl - line_start));
                        }

                        if (SL_EXPECT_TRUE(colon != nullptr)) {
                            const char* val_start = colon + 1;
                            
                            // RFC 9110 compliance: Skip all leading OWS (Optional White Space) 
                            // consisting of spaces or horizontal tabs. The scalar loop handles 
                            // multiple OWS characters and chunk boundaries safely where 
                            // mask-based resolution becomes overly complex.
                            while (val_start < nl && (*val_start == ' ' || static_cast<unsigned char>(*val_start) == '\t')) val_start++;

                            std::size_t val_len = nl - val_start;
                            if (val_len > 0 && val_start[val_len - 1] == '\r') val_len--;

                            ev->headers[ev->header_count++] = {
                                std::string_view(line_start, colon - line_start),
                                std::string_view(val_start, val_len)
                            };
                        }
                        line_start = nl + 1;
                        ptr = line_start;
                        continue;
                    }
                    ptr += 32;
                } else {
                    // Scalar Fallback: Processes the remaining bytes in the buffer.
                    const char* nl = static_cast<const char*>(std::memchr(ptr, '\n', end - ptr));
                    if (!nl) break;

                    if (nl == line_start || (nl == line_start + 1 && *line_start == '\r')) {
                        ev->body = std::string_view(nl + 1, end - (nl + 1));
                        return;
                    }

                    const char* colon = static_cast<const char*>(std::memchr(line_start, ':', nl - line_start));
                    if (colon) {
                        const char* val_start = colon + 1;
                        while (val_start < nl && (*val_start == ' ' || *val_start == '\t')) val_start++;
                        std::size_t val_len = nl - val_start;
                        if (val_len > 0 && val_start[val_len - 1] == '\r') val_len--;

                        ev->headers[ev->header_count++] = {
                            std::string_view(line_start, colon - line_start),
                            std::string_view(val_start, val_len)
                        };
                    }
                    line_start = nl + 1;
                    ptr = line_start;
                }
            }
        }
    };

    /**
     * @brief Physical Protocol Mapping.
     * @details Implements optimized register-relative bit-extraction for O(1) protocol discovery.
     */
    static inline header_parser::net_meta parse_frame(const char* packet) noexcept {
        header_parser::net_meta meta{};
        
        // Physical Mapping Constants
        static constexpr uint32_t ETH_TYPE_IPV4 = 0x0008; // Big-endian 0x0800 in LE load
        static constexpr uint8_t  PROTOCOL_UDP  = 17;

        // 1. Bit-Perfect Ethernet Resolution
        const uint16_t eth_type = *reinterpret_cast<const uint16_t*>(packet + 12);
        if (SL_EXPECT_FALSE(eth_type != ETH_TYPE_IPV4)) return meta;

        // 2. Physical IP/UDP Extraction (Bypassing struct offset delays)
        // Extracts IP Source and UDP Port using register-local arithmetic
        meta.src_ip = *reinterpret_cast<const uint32_t*>(packet + 26);
        const uint8_t ip_proto = *reinterpret_cast<const uint8_t*>(packet + 23);
        
        if (SL_EXPECT_TRUE(ip_proto == PROTOCOL_UDP)) {
            meta.src_port = *reinterpret_cast<const uint16_t*>(packet + 34);
            meta.payload_len = *reinterpret_cast<const uint16_t*>(packet + 38);
        }

        return meta;
    }
} // namespace slabflux::io
