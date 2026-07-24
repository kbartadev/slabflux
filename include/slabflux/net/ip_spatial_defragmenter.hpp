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
 * ============================================================================* @file ip_spatial_defragmenter.hpp
 * @brief Zero-allocation IPv4 Spatial Reassembly Matrix.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/net/raw_tcp_ipv4_frame.hpp"
#include "slabflux/core/endian.hpp"
#include "slabflux/net/tcp_wire_engine.hpp"
#include <x86intrin.h> // For hardware CRC32

namespace slabflux::net {

    struct alignas(64) ip_reassembly_slot {
        uint32_t src_ip{0};
        uint32_t dst_ip{0};
        uint16_t ip_id{0};
        uint8_t  protocol{0};
        bool     active{false};
        
        uint16_t total_expected_length{0};
        uint16_t current_bytes_received{0};
        uint32_t highest_byte_received{0}; // CRITICAL: Contiguity Guard
        uint64_t first_active_ts{0};

        uint64_t presence_mask[1024]{0}; // 1 bit per byte bounding exactly 64KB

        // Flat matrix mapped to maximum allowed IP datagram size (64KB)
        alignas(64) char buffer[65536];
    };

    template<size_t Capacity = 1024>
    class alignas(64) ip_spatial_defragmenter {
        ip_reassembly_slot slots_[Capacity];

    public:
        ip_spatial_defragmenter() {
            std::memset(slots_, 0, sizeof(slots_));
        }

        /**
         * @brief Integrates fragmented datagrams in O(1) via Spatial Hashing.
         * @return A synthesized contiguous raw frame if complete, nullptr otherwise.
         */
        SLAB_HOT raw_tcp_ipv4_frame* process_fragment(const raw_tcp_ipv4_frame* frag_hdr, size_t physical_length, uint64_t now) noexcept {
            // CRITICAL FIX: IP Defragmentation Hash Assassination (CVE-2018-5391 Variant)
            // Use hardware CRC32 to cryptographically scatter fragments, neutralizing XOR collision DoS.
            uint64_t mix = (static_cast<uint64_t>(frag_hdr->ip_src) << 32) | frag_hdr->ip_dst;
            mix ^= (static_cast<uint64_t>(frag_hdr->ip_id) << 16) | frag_hdr->ip_protocol;
            uint32_t hash = _mm_crc32_u64(0xDEADBEEF, mix) & (Capacity - 1);
            
            // CRITICAL FIX: Axiom 3 - Cryptographic Hash Assassination in Spatial Defragmentation
            ip_reassembly_slot* target_slot = nullptr;
            for (size_t i = 0; i < 8; ++i) {
                uint32_t idx = (hash + i) & (Capacity - 1);
                auto& slot = slots_[idx];
                
                if (slot.active && slot.src_ip == frag_hdr->ip_src && slot.ip_id == frag_hdr->ip_id) {
                    target_slot = &slot;
                    break;
                }
                // CRITICAL FIX: Absolute Timeout prevents Slowloris cache starvation
                if (!slot.active || (now - slot.first_active_ts > 30000)) {
                    std::memset(&slot, 0, offsetof(ip_reassembly_slot, buffer)); // Zero meta and masks
                    slot.src_ip = frag_hdr->ip_src;
                    slot.dst_ip = frag_hdr->ip_dst;
                    slot.ip_id = frag_hdr->ip_id;
                    slot.protocol = frag_hdr->ip_protocol;
                    slot.active = true;
                    slot.first_active_ts = now;
                    slot.highest_byte_received = 0;
                    target_slot = &slot;
                    break;
                }
            }
            if (SL_EXPECT_FALSE(!target_slot)) return nullptr; // Hash buckets exhausted
            auto& slot = *target_slot;

            uint16_t frag_offset_val = core::endian::network_to_host16(frag_hdr->ip_frag_offset);
            bool more_fragments = (frag_offset_val & 0x2000) != 0;
            uint16_t offset_bytes = (frag_offset_val & 0x1FFF) * 8;
            uint16_t header_len = (frag_hdr->ip_ihl_ver & 0x0F) * 4;
            uint16_t ip_len_host = core::endian::network_to_host16(frag_hdr->ip_len);
            
            // CRITICAL: Prevent integer underflow if ip_len is maliciously spoofed smaller than the header length
            if (SL_EXPECT_FALSE(ip_len_host < header_len)) return nullptr;
            if (SL_EXPECT_FALSE(ip_len_host > physical_length - 14)) return nullptr; // CRITICAL: Prevent OOB Read
            uint16_t payload_len = ip_len_host - header_len;

            if (SL_EXPECT_FALSE(offset_bytes + payload_len > 65535)) return nullptr; // Structural Malformation
            if (SL_EXPECT_FALSE(payload_len == 0)) return nullptr; // Empty payloads bypass mask checks

            // Branchless Overlap Verification using SIMD-style word masks
            uint32_t start_word = offset_bytes / 64;
            uint32_t end_word = (offset_bytes + payload_len) / 64;
            uint32_t start_bit = offset_bytes % 64;
            uint32_t end_bit = (offset_bytes + payload_len) % 64;

            if (start_word == end_word) {
                uint64_t mask = (payload_len == 64) ? ~0ULL : (((1ULL << payload_len) - 1) << start_bit);
                if (slot.presence_mask[start_word] & mask) return nullptr; // Overlap Drop
                slot.presence_mask[start_word] |= mask;
            } else {
                if (slot.presence_mask[start_word] & (~0ULL << start_bit)) return nullptr;
                for (uint32_t i = start_word + 1; i < end_word; ++i) {
                    if (slot.presence_mask[i] != 0) return nullptr;
                }
                if (end_bit > 0 && (slot.presence_mask[end_word] & ((1ULL << end_bit) - 1))) return nullptr;

                slot.presence_mask[start_word] |= (~0ULL << start_bit);
                for (uint32_t i = start_word + 1; i < end_word; ++i) {
                    slot.presence_mask[i] = ~0ULL;
                }
                if (end_bit > 0) {
                    slot.presence_mask[end_word] |= (1ULL << end_bit) - 1;
                }
            }

            const char* payload_ptr = reinterpret_cast<const char*>(frag_hdr) + 14 + header_len;
            std::memcpy(slot.buffer + offset_bytes, payload_ptr, payload_len);
            slot.current_bytes_received += payload_len;
            
            if (offset_bytes + payload_len > slot.highest_byte_received) {
                slot.highest_byte_received = offset_bytes + payload_len;
            }

            if (!more_fragments) {
                slot.total_expected_length = offset_bytes + payload_len;
            }

            // Reassembly trigger
            // CRITICAL FIX: Prevent Information Leak via Gapped Reassembly
            // Validates that no bytes were received outside the terminal boundary, mathematically 
            // proving that the internal bytes must be 100% contiguous to satisfy the length check.
            if (slot.total_expected_length > 0 && slot.current_bytes_received == slot.total_expected_length && slot.highest_byte_received == slot.total_expected_length) {
                slot.active = false;
                
                // Reconstruct native framing on the local thread stack for upstream injection
                alignas(64) static thread_local char reassembled_frame[65536 + 128]; // CRITICAL FIX: Jumbo Fragmentation Stack Overflow
                std::memcpy(reassembled_frame, frag_hdr, 14 + header_len);
                std::memcpy(reassembled_frame + 14 + header_len, slot.buffer, slot.total_expected_length);
                
                auto* final_hdr = reinterpret_cast<raw_tcp_ipv4_frame*>(reassembled_frame);
                final_hdr->ip_len = core::endian::host_to_network16(header_len + slot.total_expected_length);
                final_hdr->ip_frag_offset = 0; // Wipe fragmentation bits
                final_hdr->ip_checksum = 0;
                final_hdr->ip_checksum = tcp_wire_engine::compute_checksum(&final_hdr->ip_ihl_ver, header_len, 0);
                return final_hdr;
            }
            return nullptr;
        }
    };
} // namespace slabflux::net