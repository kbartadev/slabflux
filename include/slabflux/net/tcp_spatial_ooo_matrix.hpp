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
 * ============================================================================* @file tcp_spatial_ooo_matrix.hpp
 * @brief Zero-allocation, bitmask-driven Out-Of-Order TCP segment reassembly.
 */

#pragma once

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <rte_mbuf.h>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::net {

    /**
     * @brief Flat Matrix for OOO Reassembly.
     * @details Tracks a 64KB TCP receive window. Uses an 8KB presence mask 
     * (1 bit per byte) to eliminate complex linked lists. Evaluated using SIMD 
     * or hardware POPCNT/CTZ instructions. Retains raw DPDK mbufs to eliminate copies.
     */
    struct alignas(64) tcp_spatial_ooo_matrix {
        static constexpr uint32_t WINDOW_SIZE = 65536;
        static constexpr uint32_t MASK_LONGS = WINDOW_SIZE / 64; // 1024 uint64_t
        static constexpr uint32_t MAX_MBUFS = 128;               // Up to 128 OOO packets

        uint64_t presence_mask[MASK_LONGS]; // 1 bit per byte
        
        struct alignas(16) mbuf_slot {
            uint32_t seq;
            uint32_t len;
            struct rte_mbuf* mbuf;
            uint16_t payload_offset;
            uint8_t  flags; // Preserve TCP payload markers (FIN/SYN)
        };

        mbuf_slot slots_[MAX_MBUFS];
        uint64_t active_slots_[2]; // 128 bits for O(1) slot allocation
        
        uint32_t base_seq{0}; // Modulo sequence base tied to buffer[0]

        tcp_spatial_ooo_matrix() {
            std::memset(presence_mask, 0, sizeof(presence_mask));
            std::memset(slots_, 0, sizeof(slots_));
            active_slots_[0] = 0;
            active_slots_[1] = 0;
        }

        SLAB_FORCE_INLINE void reset(uint32_t expected_next_seq) noexcept {
            std::memset(presence_mask, 0, sizeof(presence_mask));
            for (int i = 0; i < 2; ++i) {
                uint64_t active = active_slots_[i];
                while (active != 0) {
                    uint32_t bit = __builtin_ctzll(active);
                    uint32_t idx = i * 64 + bit;
                    if (slots_[idx].mbuf) {
                        rte_pktmbuf_free(slots_[idx].mbuf);
                        slots_[idx].mbuf = nullptr;
                    }
                    active &= active - 1;
                }
                active_slots_[i] = 0;
            }
            base_seq = expected_next_seq;
        }

        /**
         * @brief Rapid mathematical bitmask shift to aggressively rebase the spatial matrix.
         * @details Preserves zero-copy MBUF structures while dynamically expanding the trailing horizon.
         */
        SLAB_HOT void slide_window(uint32_t current_rcv_nxt) noexcept {
            if (current_rcv_nxt == base_seq) return;
            
            // CRITICAL FIX: Axiom 22 - Out-of-Order Matrix Annihilation via Asymmetric Sequence Wrap
            if (SL_EXPECT_FALSE(static_cast<int32_t>(current_rcv_nxt - base_seq) <= 0)) return;
            
            uint32_t shift_bytes = current_rcv_nxt - base_seq;
            if (shift_bytes >= WINDOW_SIZE) {
                reset(current_rcv_nxt);
                return;
            }

            uint32_t shift_words = shift_bytes / 64;
            uint32_t shift_bits = shift_bytes % 64;

            if (shift_words > 0) {
                std::memmove(presence_mask, presence_mask + shift_words, (MASK_LONGS - shift_words) * 8);
                std::memset(presence_mask + (MASK_LONGS - shift_words), 0, shift_words * 8);
            }

            if (shift_bits > 0) {
                for (uint32_t i = 0; i < MASK_LONGS - 1; ++i) {
                    presence_mask[i] = (presence_mask[i] >> shift_bits) | (presence_mask[i + 1] << (64 - shift_bits));
                }
                presence_mask[MASK_LONGS - 1] >>= shift_bits;
            }

            base_seq = current_rcv_nxt;
        }

        /**
         * @brief Stores the raw DPDK hardware buffer natively.
         */
        SLAB_HOT bool insert_mbuf(uint32_t current_rcv_nxt, uint32_t seq, struct rte_mbuf* mbuf, uint16_t payload_offset, uint32_t len, uint8_t flags) noexcept {
            // CRITICAL FIX: Axiom 8 - OOO Pure FIN Sequence Extraction
            uint32_t logical_len = len;
            if (SL_EXPECT_FALSE(len == 0 && (flags & 0x01))) {
                logical_len = 1;
            }
            if (SL_EXPECT_FALSE(logical_len == 0 || !mbuf)) return false;

            if (SL_EXPECT_FALSE(seq - base_seq >= WINDOW_SIZE)) {
                slide_window(current_rcv_nxt);
            }

            uint32_t offset = seq - base_seq; 
            if (SL_EXPECT_FALSE(offset >= WINDOW_SIZE)) return false; // Out of bounds

            uint32_t safe_len = std::min(logical_len, WINDOW_SIZE - offset);
            
            // CRITICAL FIX: Axiom 29 - Teardrop Fracture in Orthogonal Spatial Matrix Intersections
            // Iteratively trim the overlapping head and tail coordinates, maintaining continuous orthogonal geometry.
            while (safe_len > 0 && (presence_mask[offset / 64] & (1ULL << (offset % 64)))) {
                offset++;
                payload_offset++;
                seq++;
                safe_len--;
                logical_len--;
                if (len > 0) len--;
            }
            while (safe_len > 0 && (presence_mask[(offset + safe_len - 1) / 64] & (1ULL << ((offset + safe_len - 1) % 64)))) {
                safe_len--;
                logical_len--;
                if (len > 0) len--;
            }
            if (SL_EXPECT_FALSE(safe_len == 0)) return false; // Completely enveloped by existing state

            uint32_t start_word = offset / 64;
            uint32_t end_word = (offset + safe_len) / 64;
            uint32_t start_bit = offset % 64;
            uint32_t end_bit = (offset + safe_len) % 64;

            // Full Range Overlap Verification: Prevent partial intersections (Teardrop vector)
            // from corrupting the spatial matrix bitmasks and leaking mbufs.
            if (start_word == end_word) {
                uint64_t mask = (safe_len == 64) ? ~0ULL : (((1ULL << safe_len) - 1) << start_bit);
                if ((presence_mask[start_word] & mask) != 0) return false;
            } else {
                // Axiom 24: Lexicographical Disjunction via Bitwise Modulo Horizons
                uint64_t start_mask = (start_bit == 0) ? ~0ULL : (~0ULL << start_bit);
                if ((presence_mask[start_word] & start_mask) != 0) return false;
                for (uint32_t i = start_word + 1; i < end_word; ++i) {
                    if (presence_mask[i] != 0) return false;
                }
                if (end_bit > 0 && (presence_mask[end_word] & ((1ULL << end_bit) - 1)) != 0) return false;
            }

            // CRITICAL FIX: Axiom 36 - Wrapped segment overlap verification
            if (SL_EXPECT_FALSE(logical_len > safe_len)) {
                uint32_t rem_len = logical_len - safe_len;
                uint32_t rem_end_word = rem_len / 64;
                uint32_t rem_end_bit = rem_len % 64;
                for (uint32_t i = 0; i < rem_end_word; ++i) {
                    if (presence_mask[i] != 0) return false;
                }
                if (rem_end_bit > 0 && (presence_mask[rem_end_word] & ((1ULL << rem_end_bit) - 1)) != 0) return false;
            }

            // Find a free slot in O(1)
            uint32_t slot_idx = 0xFFFFFFFF;
            if (~active_slots_[0] != 0) {
                slot_idx = __builtin_ctzll(~active_slots_[0]);
                active_slots_[0] |= (1ULL << slot_idx);
            } else if (~active_slots_[1] != 0) {
                slot_idx = 64 + __builtin_ctzll(~active_slots_[1]);
                active_slots_[1] |= (1ULL << (slot_idx - 64));
            }

            if (SL_EXPECT_FALSE(slot_idx == 0xFFFFFFFF)) {
                // CRITICAL FIX: Axiom 12 - Principal Fiber Bundle Saturation via Asymmetric Metric Injections
                // Saturated unacknowledged sequence boundaries must collapse the current state and force 
                // a native retransmit sequence to preserve the homological chain.
                reset(base_seq);
                return false;
            }

            // CRITICAL FIX: Axiom 1 - Topological Rupture in Spatial Truncation Matrices
            if (SL_EXPECT_FALSE(logical_len > safe_len)) {
                len = safe_len;
                flags &= ~0x01;
            }

            slots_[slot_idx].seq = seq;
            slots_[slot_idx].len = len;
            slots_[slot_idx].mbuf = mbuf;
            slots_[slot_idx].payload_offset = payload_offset;
            slots_[slot_idx].flags = flags;

            if (start_word == end_word) {
                uint64_t mask = (safe_len == 64) ? ~0ULL : (((1ULL << safe_len) - 1) << start_bit);
                presence_mask[start_word] |= mask;
            } else {
                uint64_t start_mask = (start_bit == 0) ? ~0ULL : (~0ULL << start_bit);
                presence_mask[start_word] |= start_mask;
                for (uint32_t i = start_word + 1; i < end_word; ++i) {
                    presence_mask[i] = ~0ULL;
                }
                if (end_bit > 0) {
                    presence_mask[end_word] |= (1ULL << end_bit) - 1;
                }
            }

            if (SL_EXPECT_FALSE(logical_len > safe_len)) {
                uint32_t rem_len = logical_len - safe_len;
                uint32_t rem_end_word = rem_len / 64;
                uint32_t rem_end_bit = rem_len % 64;
                for (uint32_t i = 0; i < rem_end_word; ++i) {
                    presence_mask[i] |= ~0ULL;
                }
                if (rem_end_bit > 0) {
                    presence_mask[rem_end_word] |= (1ULL << rem_end_bit) - 1;
                }
            }
            return true;
        }

        /**
         * @brief Retrieves the next contiguous DPDK mbuf exactly in sequence order.
         */
        SLAB_HOT struct rte_mbuf* extract_contiguous_mbuf(uint32_t current_rcv_nxt, uint32_t& out_len, uint16_t& out_offset, uint8_t& out_flags) noexcept {
            uint32_t offset = current_rcv_nxt - base_seq;
            if (SL_EXPECT_FALSE(offset >= WINDOW_SIZE)) return nullptr;

            // Is the exact expected sequence byte present in the matrix?
            if ((presence_mask[offset / 64] & (1ULL << (offset % 64))) == 0) return nullptr;

            for (int i = 0; i < 2; ++i) {
                uint64_t active = active_slots_[i];
                while (active != 0) {
                    uint32_t bit = __builtin_ctzll(active);
                    uint32_t idx = i * 64 + bit;
                    
                    if (slots_[idx].seq == current_rcv_nxt) {
                        struct rte_mbuf* mbuf = slots_[idx].mbuf;
                        out_len = slots_[idx].len;
                        out_offset = slots_[idx].payload_offset;
                        out_flags = slots_[idx].flags;
                        
                        slots_[idx].mbuf = nullptr;
                        if (i == 0) active_slots_[0] &= ~(1ULL << bit);
                        else active_slots_[1] &= ~(1ULL << bit);

                        uint32_t logical_len = out_len;
                        if (SL_EXPECT_FALSE(out_len == 0 && (out_flags & 0x01))) {
                            logical_len = 1;
                        }

                        uint32_t safe_len = std::min(logical_len, WINDOW_SIZE - offset);
                        uint32_t start_word = offset / 64;
                        uint32_t end_word = (offset + safe_len) / 64;
                        uint32_t start_bit = offset % 64;
                        uint32_t end_bit = (offset + safe_len) % 64;

                        if (start_word == end_word) {
                            uint64_t mask = (safe_len == 64) ? ~0ULL : (((1ULL << safe_len) - 1) << start_bit);
                            presence_mask[start_word] &= ~mask;
                        } else {
                            uint64_t start_mask = (start_bit == 0) ? 0ULL : ~(~0ULL << start_bit);
                            presence_mask[start_word] &= start_mask;
                            for (uint32_t j = start_word + 1; j < end_word; ++j) {
                                presence_mask[j] = 0ULL;
                            }
                            if (end_bit > 0) {
                                presence_mask[end_word] &= ~((1ULL << end_bit) - 1);
                            }
                        }

                        // CRITICAL FIX: Axiom 36 - Isomorphic Ghost Coordinates in Matrix Extraction
                        if (SL_EXPECT_FALSE(logical_len > safe_len)) {
                            uint32_t rem_len = logical_len - safe_len;
                            uint32_t rem_end_word = rem_len / 64;
                            uint32_t rem_end_bit = rem_len % 64;
                            for (uint32_t j = 0; j < rem_end_word; ++j) {
                                presence_mask[j] = 0ULL;
                            }
                            if (rem_end_bit > 0) {
                                presence_mask[rem_end_word] &= ~((1ULL << rem_end_bit) - 1);
                            }
                        }

                        // Automatically advance the geometric base if the matrix drains perfectly
                        if (active_slots_[0] == 0 && active_slots_[1] == 0) {
                            base_seq = current_rcv_nxt + logical_len;
                        } else if (offset + safe_len >= WINDOW_SIZE) {
                            // CRITICAL FIX: Axiom 10 - Lexicographical Disjunction in OOO Extractor Desynchronization
                            slide_window(current_rcv_nxt + logical_len);
                        }
                        
                        return mbuf;
                    }
                    active &= active - 1;
                }
            }

            return nullptr;
        }
    };

} // namespace slabflux::net