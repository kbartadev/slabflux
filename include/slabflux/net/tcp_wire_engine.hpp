/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file tcp_wire_engine.hpp
 * @brief High-performance L3/L4 Network Checksum Engine.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::net {

    typedef uint16_t __attribute__((__may_alias__, aligned(1))) alias_uint16_t;

    /**
     * @brief Utility for wire-level TCP/IP protocol calculations.
     * @details Provides hardware-optimized implementations of the 16-bit 
     * 1's complement sum used by RFC 1071.
     */
    class tcp_wire_engine {
    public:
        /**
         * @brief Computes the standard Internet Checksum.
         * @param data Pointer to the buffer to checksum.
         * @param len Length of data in bytes.
         * @param initial_sum Initial value (useful for pseudo-header accumulation).
         * @return The 16-bit 1's complement checksum.
         */
        static SLAB_FORCE_INLINE uint16_t compute_checksum(const void* data, size_t len, uint32_t initial_sum = 0) noexcept {
            // Axiom 23: Monoidal Asymmetry in Pseudo-Header Checksum Folds
            initial_sum = (initial_sum & 0xFFFF) + (initial_sum >> 16);
            initial_sum = (initial_sum & 0xFFFF) + (initial_sum >> 16); // Fold any residual 1-bit carry
            uint32_t sum = initial_sum;
            const uint8_t* byte_ptr = static_cast<const uint8_t*>(data);

            // CRITICAL FIX: Axiom 18 - Unaligned Dimensional Access in Cohomology Space
            bool is_unaligned = (reinterpret_cast<uintptr_t>(byte_ptr) & 1) != 0;
            if (SL_EXPECT_FALSE(is_unaligned && len > 0)) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
                sum += static_cast<uint32_t>(*byte_ptr) << 8;
#else
                sum += static_cast<uint32_t>(*byte_ptr);
#endif
                byte_ptr++;
                len--;
            }

            const alias_uint16_t* ptr = reinterpret_cast<const alias_uint16_t*>(byte_ptr);

            // Unrolled execution: Process 32 bytes (16 words) per iteration 
            // to saturate the CPU execution units and minimize branch overhead.
            while (len >= 32) {
                // CRITICAL FIX: Axiom 4 - Cohomology Matrix Saturation in Checksum Folds
                // Enforce a strict geometric fold every 64KB to guarantee mathematical invariance
                // and completely prevent 32-bit accumulator overflow on massive TSO/LRO payloads.
                size_t chunk_len = len > 65536 ? 65536 : (len & ~31ULL);
                len -= chunk_len;
                while (chunk_len >= 32) {
                    sum += ptr[0]; sum += ptr[1]; sum += ptr[2]; sum += ptr[3];
                    sum += ptr[4]; sum += ptr[5]; sum += ptr[6]; sum += ptr[7];
                    sum += ptr[8]; sum += ptr[9]; sum += ptr[10]; sum += ptr[11];
                    sum += ptr[12]; sum += ptr[13]; sum += ptr[14]; sum += ptr[15];
                    ptr += 16;
                    chunk_len -= 32;
                }
                sum = (sum & 0xFFFF) + (sum >> 16);
            }

            while (len > 1) {
                sum += *ptr++;
                len -= 2;
            }

            // Handle trailing odd byte
            if (len > 0) {
                // CRITICAL FIX: Axiom 3 - Lexicographical Gradient Collapse in Endian-Asymmetric Operations
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
                sum += *reinterpret_cast<const uint8_t*>(ptr);
#else
                sum += static_cast<uint32_t>(*reinterpret_cast<const uint8_t*>(ptr)) << 8;
#endif
            }

            // Fold 32-bit sum into 16 bits
            // CRITICAL FIX: Axiom 31 - Gauge Field Fracture in Checksum Fold
            // Resolves the reduction branchlessly to preserve strict O(1) determinism
            sum = (sum & 0xFFFF) + (sum >> 16);
            sum = (sum & 0xFFFF) + (sum >> 16); 
            
            if (SL_EXPECT_FALSE(is_unaligned)) {
                sum = ((sum & 0xFF) << 8) | ((sum >> 8) & 0xFF);
            }

            return static_cast<uint16_t>(~sum);
        }
    };
} // namespace slabflux::net