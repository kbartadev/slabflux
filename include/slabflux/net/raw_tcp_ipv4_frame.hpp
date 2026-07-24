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

#include <cstdint>

namespace slabflux::net {

    // Structural Fusion: Ethernet II, IPv4, and TCP header geometries.
    // Exactly 64 bytes to accommodate standard headers plus dynamic TCP Options appending.
#pragma pack(push, 1)
    struct alignas(64) raw_tcp_ipv4_frame {
        // --- Layer 2: Ethernet II (14 Bytes) ---
        uint8_t  dest_mac[6];
        uint8_t  src_mac[6];
        uint16_t eth_type;

        // --- Layer 3: IPv4 (20 Bytes) ---
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

        // --- Layer 4: TCP (20 Bytes) ---
        uint16_t tcp_src_port;
        uint16_t tcp_dst_port;
        uint32_t tcp_seq;
        uint32_t tcp_ack;
        uint8_t  tcp_data_offset;
        uint8_t  tcp_flags;
        uint16_t tcp_window;
        uint16_t tcp_checksum;
        uint16_t tcp_urgent_ptr;

        uint8_t  tcp_options[10]; // Explicit pad to 64 bytes, holds dynamic MSS/WScale
    };
#pragma pack(pop)

} // namespace slabflux::net