/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * @file raw_udp_ipv6_frame.hpp
 * @brief Stateless, packed physical memory map for an IPv6 UDP datagram.
 */

#pragma once
#include <cstdint>

namespace slabflux::net {

#pragma pack(push, 1)
    struct alignas(64) raw_udp_ipv6_frame {
        // --- Layer 2: Ethernet II (14 Bytes) ---
        uint8_t  dest_mac[6];
        uint8_t  src_mac[6];
        uint16_t eth_type; // 0x86DD

        // --- Layer 3: IPv6 (40 Bytes) ---
        uint32_t ipv6_flow;
        uint16_t ipv6_plen; // Payload length (UDP Header + Data)
        uint8_t  ipv6_nxt;  // Next header (17 for UDP)
        uint8_t  ipv6_hlim;
        uint64_t ipv6_src[2];
        uint64_t ipv6_dst[2];

        // --- Layer 4: UDP (8 Bytes) ---
        uint16_t udp_src_port;
        uint16_t udp_dst_port;
        uint16_t udp_length;
        uint16_t udp_checksum;
        
        uint8_t  _padding[2]; // Math padding to 64 bytes total
    };
#pragma pack(pop)

    static_assert(sizeof(raw_udp_ipv6_frame) == 64, "IPv6 UDP frame geometry must perfectly map to 64 bytes for L1 isolation.");
    static_assert(offsetof(raw_udp_ipv6_frame, udp_src_port) == 54, "UDP header must start at exactly offset 54 in IPv6");

} // namespace slabflux::net