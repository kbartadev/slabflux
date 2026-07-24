/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * @file raw_tcp_ipv6_frame.hpp
 * @brief Stateless, packed physical memory map for an IPv6 TCP segment.
 */

#pragma once
#include <cstdint>

namespace slabflux::net {

#pragma pack(push, 1)
    struct alignas(64) raw_tcp_ipv6_frame {
        // --- Layer 2: Ethernet II (14 Bytes) ---
        uint8_t  dest_mac[6];
        uint8_t  src_mac[6];
        uint16_t eth_type; // 0x86DD

        // --- Layer 3: IPv6 (40 Bytes) ---
        uint32_t ipv6_flow; // Version (4), Traffic Class (8), Flow Label (20)
        uint16_t ipv6_plen; // Payload length (Excludes the 40-byte IPv6 header!)
        uint8_t  ipv6_nxt;  // Next header (6 for TCP)
        uint8_t  ipv6_hlim; // Hop limit
        uint64_t ipv6_src[2];
        uint64_t ipv6_dst[2];

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

        // --- TCP Options (54 Bytes) ---
        // Total base length is 14 + 40 + 20 = 74 bytes.
        // Padding options out to 54 bytes maps the entire struct 
        // perfectly to 128 bytes (2x 64-byte Cache Lines).
        uint8_t  tcp_options[54]; 
    };
#pragma pack(pop)

    // Enforce hardware invariants
    static_assert(sizeof(raw_tcp_ipv6_frame) == 128, "IPv6 TCP frame geometry must perfectly map to 128 bytes for L1 isolation.");
    static_assert(offsetof(raw_tcp_ipv6_frame, ipv6_flow) == 14, "IPv6 header must start at exactly offset 14");
    static_assert(offsetof(raw_tcp_ipv6_frame, tcp_src_port) == 54, "TCP header must start at exactly offset 54 in IPv6");

} // namespace slabflux::net