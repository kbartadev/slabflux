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
 * ============================================================================*
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 * Absolute Liability Limitation & Full Terms: See DISCLAIMER, NOTICE, LICENSE.
 *
 * @file stack.hpp
 * @brief Minimalist User-space Network Responder with In-Place Register Swapping.
 * @details Implements performance-optimized networking field resolution using register-local access.
 */

#pragma once
#include <cstring>
#include "slabflux/io/header_parser.hpp"

namespace slabflux::io {

    class stack {
    public:
        /**
         * @brief Infrastructural ARP/ICMP handler with zero slow memcpy calls.
         */
        inline bool handle_infrastructure_traffic(const void* frame, void* tx_buffer) noexcept {
            const uint8_t* p = static_cast<const uint8_t*>(frame);
            uint8_t* resp = static_cast<uint8_t*>(tx_buffer);

            // Physical Constant Mapping
            static constexpr uint16_t ETH_TYPE_ARP  = 0x0608; // Big-endian in LE load
            static constexpr uint16_t ETH_TYPE_IPV4 = 0x0008; 
            static constexpr uint16_t ARP_OP_REQ    = 0x0100;
            static constexpr uint8_t  IP_PROTO_ICMP_V4 = 1;
            static constexpr uint8_t  ICMP_ECHO_REQUEST = 8;

            const uint16_t eth_type = *reinterpret_cast<const uint16_t*>(p + 12);

            // 1. ARP Resolution (Offset 14)
            if (eth_type == ETH_TYPE_ARP) {
                if (*reinterpret_cast<const uint16_t*>(p + 20) != ARP_OP_REQ) return true;

                // Direct Register-Level Header Reconstruction
                std::memcpy(resp, p + 6, 6);              // Dest MAC = Src MAC
                std::memcpy(resp + 6, "\xDE\xAD\xBE\xEF\xCA\xFE", 6); // Local MAC
                *reinterpret_cast<uint16_t*>(resp + 12) = ETH_TYPE_ARP;

                std::memcpy(resp + 14, p + 14, 6);        // HW/Protocol Type/Size
                *reinterpret_cast<uint16_t*>(resp + 20) = 0x0200; // ARP_OP_REPLY (0x0002)

                std::memcpy(resp + 22, resp + 6, 6);      // Sender MAC = Local MAC
                std::memcpy(resp + 28, p + 38, 4);        // Sender IP = Target IP
                std::memcpy(resp + 32, p + 22, 6);        // Target MAC = Request Sender MAC
                std::memcpy(resp + 38, p + 28, 4);        // Target IP = Request Sender IP

                return true;
            }

            // 2. ICMP Echo (Offset 34)
            if (eth_type == ETH_TYPE_IPV4) {
                if (*(p + 23) == IP_PROTO_ICMP_V4) {
                    const uint8_t* ip_base = p + 14;
                    const size_t ip_hdr_len = (*(p + 14) & 0x0F) * 4;
                    const uint8_t* icmp_base = ip_base + ip_hdr_len;

                    if (*icmp_base != ICMP_ECHO_REQUEST) return false;

                    // Bit-Perfect Header Inversion
                    std::memcpy(resp, p + 6, 6);          // Dest MAC
                    std::memcpy(resp + 6, p, 6);          // Src MAC
                    *reinterpret_cast<uint16_t*>(resp + 12) = ETH_TYPE_IPV4;

                    std::memcpy(resp + 14, ip_base, 20);  // Clone IP Header
                    *reinterpret_cast<uint32_t*>(resp + 26) = *reinterpret_cast<const uint32_t*>(ip_base + 16); // Src = Dest
                    *reinterpret_cast<uint32_t*>(resp + 30) = *reinterpret_cast<const uint32_t*>(ip_base + 12); // Dest = Src

                    const uint16_t ip_len = __builtin_bswap16(*reinterpret_cast<const uint16_t*>(ip_base + 2));
                    if (SL_EXPECT_FALSE(ip_len < ip_hdr_len)) return false; 
                    
                    const size_t icmp_len = ip_len - ip_hdr_len;
                    uint8_t* resp_icmp = resp + 14 + ip_hdr_len;

                    std::memcpy(resp_icmp, icmp_base, icmp_len);
                    *resp_icmp = 0; // ECHO_REPLY type
                    *reinterpret_cast<uint16_t*>(resp_icmp + 2) = 0; // Clear checksum
                    *reinterpret_cast<uint16_t*>(resp_icmp + 2) = calculate_checksum(resp_icmp, icmp_len);

                    return true;
                }
            }

            return false;
        }

    private:
        /**
         * @brief Parallel Word Reduction.
         * @details Implements optimized RFC 1071 checksum calculation 
         * for improved response 
         * integrity verification.
         */
        static inline uint16_t calculate_checksum(void* b, int len) noexcept {
            uint64_t sum = 0;
            auto* buf = static_cast<uint16_t*>(b);
            while (len > 1) { sum += *buf++; len -= 2; }
            if (len == 1) sum += *reinterpret_cast<const uint8_t*>(buf);
            
            // Final Fold
            sum = (sum >> 16) + (sum & 0xFFFF);
            return static_cast<uint16_t>(~(sum + (sum >> 16)));
        }
    };
}
