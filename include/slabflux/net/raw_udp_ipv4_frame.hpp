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
 * ============================================================================* @file raw_udp_ipv4_frame.hpp
 * @brief Stateless, packed physical memory map for a standard UDP datagram.
 */

#pragma once

#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::net {

    /**
     * @brief Flat, packed physical memory map for a standard UDP/IPv4 frame.
     * @details Perfectly bounds to 42 bytes (14 Eth + 20 IP + 8 UDP).
     */
    #pragma pack(push, 1)
    struct raw_udp_ipv4_frame {
        // Layer 2: Ethernet
        uint8_t  dest_mac[6];
        uint8_t  src_mac[6];
        uint16_t eth_type;

        // Layer 3: IPv4
        uint8_t  ip_ihl_ver;
        uint8_t  ip_tos;
        uint16_t ip_len;
        uint16_t ip_id;
        uint16_t ip_frag_offset;
        uint8_t  ip_ttl;
        uint8_t  ip_protocol;
        uint16_t ip_checksum;
        uint32_t ip_src;
        uint32_t ip_dst;

        // Layer 4: UDP
        uint16_t udp_src_port;
        uint16_t udp_dst_port;
        uint16_t udp_length;
        uint16_t udp_checksum;
    };
    #pragma pack(pop)

    static_assert(sizeof(raw_udp_ipv4_frame) == 42, "raw_udp_ipv4_frame must be exactly 42 bytes");

} // namespace slabflux::net