/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * @file ip_spatial_defragmenter_ipv6.hpp
 * @brief Zero-Allocation IPv6 IP Defragmenter.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/mpsc_pool.hpp"
#include "slabflux/net/raw_tcp_ipv6_frame.hpp" // For raw_ipv6_header
#include <rte_mbuf.h>

namespace slabflux::net {

    // IPv6 doesn't have a direct "iphdr" equivalent for the reassembled payload.
    // Instead, we'll store the raw IPv6 frame header for the reassembled mbuf.
    using raw_ipv6_header = raw_tcp_ipv6_frame; // Use existing header structure

    /**
     * @brief Zero-allocation IPv6 IP Defragmenter.
     * @details Reassembles fragmented IPv6 datagrams using an in-place spatial matrix.
     * Works with mbufs directly to prevent heap allocation.
     * @tparam MaxConcurrentFrags The maximum number of concurrent fragment streams.
     */
    template <size_t MaxConcurrentFrags = 1024>
    class alignas(core::CACHE_LINE_SIZE) ip_spatial_defragmenter_ipv6 {
    private:
        struct alignas(64) frag_entry {
            uint64_t src_ip[2]{0, 0};
            uint64_t dst_ip[2]{0, 0};
            uint32_t frag_id{0};
            bool active{false};
            
            uint32_t total_expected_length{0};
            uint32_t current_bytes_received{0};
            uint32_t highest_byte_received{0};
            uint64_t first_active_ts{0};

            uint64_t presence_mask[1024]{0}; // 1 bit per byte bounding exactly 64KB
            alignas(64) char buffer[65536];
            
            uint32_t header_len{0};
            uint8_t next_header{0};
            char original_header[128]{0};
        };

        frag_entry slots_[MaxConcurrentFrags];

    public:
        ip_spatial_defragmenter_ipv6() {
            std::memset(slots_, 0, sizeof(slots_));
        }

        /**
         * @brief Processes an incoming IPv6 fragment.
         */
        SLAB_HOT raw_ipv6_header* process_fragment(const raw_ipv6_header* hdr, size_t physical_length, uint64_t now) noexcept {
            const char* data = reinterpret_cast<const char*>(hdr);
            uint32_t l4_offset = 54;
            uint8_t nxt_hdr = hdr->ipv6_nxt;
            uint32_t frag_offset_val = 0;
            uint32_t frag_id = 0;
            bool more_fragments = false;
            
            while (nxt_hdr != 6 && nxt_hdr != 17 && nxt_hdr != 59) {
                if (l4_offset + 8 > physical_length) return nullptr;
                if (nxt_hdr == 44) {
                    frag_offset_val = core::endian::network_to_host16(*reinterpret_cast<const uint16_t*>(data + l4_offset + 2));
                    more_fragments = (frag_offset_val & 1) != 0;
                    frag_id = core::endian::network_to_host32(*reinterpret_cast<const uint32_t*>(data + l4_offset + 4));
                    break;
                }
                nxt_hdr = data[l4_offset];
                l4_offset += (data[l4_offset + 1] + 1) * 8;
            }
            
            if (nxt_hdr != 44) return nullptr; 
            
            uint8_t unfrag_nxt_hdr = data[l4_offset]; 
            uint32_t offset_bytes = frag_offset_val & 0xFFF8;
            uint32_t header_len = l4_offset + 8; // Length up to and including Frag Header
            uint16_t ipv6_plen = core::endian::network_to_host16(hdr->ipv6_plen);
            if (physical_length < 14 + 40 + ipv6_plen) return nullptr;
            uint32_t payload_len = 14 + 40 + ipv6_plen - header_len;
            
            if (payload_len == 0 || offset_bytes + payload_len > 65535) return nullptr;
            
            uint64_t mix = hdr->ipv6_src[0] ^ hdr->ipv6_src[1] ^ hdr->ipv6_dst[0] ^ hdr->ipv6_dst[1] ^ frag_id;
            uint32_t hash = static_cast<uint32_t>(mix ^ (mix >> 32)) & (MaxConcurrentFrags - 1);
            
            frag_entry* target_slot = nullptr;
            for (size_t i = 0; i < 8; ++i) {
                uint32_t idx = (hash + i) & (MaxConcurrentFrags - 1);
                auto& slot = slots_[idx];
                
                if (slot.active && slot.frag_id == frag_id &&
                    slot.src_ip[0] == hdr->ipv6_src[0] && slot.src_ip[1] == hdr->ipv6_src[1] &&
                    slot.dst_ip[0] == hdr->ipv6_dst[0] && slot.dst_ip[1] == hdr->ipv6_dst[1]) {
                    target_slot = &slot;
                    break;
                }
                
                if (!slot.active || (now - slot.first_active_ts > 30000)) {
                    std::memset(&slot, 0, offsetof(frag_entry, buffer));
                    slot.src_ip[0] = hdr->ipv6_src[0]; slot.src_ip[1] = hdr->ipv6_src[1];
                    slot.dst_ip[0] = hdr->ipv6_dst[0]; slot.dst_ip[1] = hdr->ipv6_dst[1];
                    slot.frag_id = frag_id;
                    slot.active = true;
                    slot.first_active_ts = now;
                    slot.header_len = l4_offset; // Unfragmentable part
                    slot.next_header = unfrag_nxt_hdr;
                    std::memcpy(slot.original_header, data, std::min<uint32_t>(l4_offset, 128));
                    target_slot = &slot;
                    break;
                }
            }
            
            if (!target_slot) return nullptr;
            auto& slot = *target_slot;
            
            uint32_t start_word = offset_bytes / 64;
            uint32_t end_word = (offset_bytes + payload_len) / 64;
            uint32_t start_bit = offset_bytes % 64;
            uint32_t end_bit = (offset_bytes + payload_len) % 64;

            if (start_word == end_word) {
                uint64_t mask = (payload_len == 64) ? ~0ULL : (((1ULL << payload_len) - 1) << start_bit);
                if (slot.presence_mask[start_word] & mask) return nullptr; // Overlap Drop
                slot.presence_mask[start_word] |= mask;
            } else {
                // Single-Pass Check-and-Set: Mitigates ILP Shadowing and Cache Pressure
                uint64_t start_mask = ~0ULL << start_bit;
                if (slot.presence_mask[start_word] & start_mask) return nullptr;
                slot.presence_mask[start_word] |= start_mask;

                for (uint32_t i = start_word + 1; i < end_word; ++i) {
                    if (slot.presence_mask[i] != 0) return nullptr;
                    slot.presence_mask[i] = ~0ULL;
                }

                if (end_bit > 0) {
                    uint64_t end_mask = (1ULL << end_bit) - 1;
                    if (slot.presence_mask[end_word] & end_mask) return nullptr;
                    slot.presence_mask[end_word] |= end_mask;
                }
            }

            std::memcpy(slot.buffer + offset_bytes, data + header_len, payload_len);
            slot.current_bytes_received += payload_len;
            
            if (offset_bytes + payload_len > slot.highest_byte_received) {
                slot.highest_byte_received = offset_bytes + payload_len;
            }

            if (!more_fragments) {
                slot.total_expected_length = offset_bytes + payload_len;
            }
            
            if (slot.total_expected_length > 0 && slot.current_bytes_received == slot.total_expected_length && slot.highest_byte_received == slot.total_expected_length) {
                slot.active = false;
                alignas(64) static thread_local char reassembled[65536 + 128];
                
                std::memcpy(reassembled, slot.original_header, slot.header_len);
                std::memcpy(reassembled + slot.header_len, slot.buffer, slot.total_expected_length);
                
                auto* final_hdr = reinterpret_cast<raw_tcp_ipv6_frame*>(reassembled);
                final_hdr->ipv6_plen = core::endian::host_to_network16(slot.header_len - 54 + slot.total_expected_length);
                
                if (slot.header_len == 54) final_hdr->ipv6_nxt = slot.next_header;
                else reassembled[slot.header_len - 8] = slot.next_header; 
                
                return final_hdr;
            }
            return nullptr;
        }
    };
} // namespace slabflux::net