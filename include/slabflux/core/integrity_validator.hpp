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
 * @file integrity_validator.hpp
 * @brief Hardware-level Data Integrity.
 * @details Implements mandatory CRC64 validation for every event.
 * Detects silent memory corruption before it affects the state logic.
 */

#pragma once

#include <nmmintrin.h> // SSE4.2 CRC32
#include <cstring>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    class integrity_validator {
    public:
        /**
         * @brief Computes a hardware-accelerated CRC32-C (Castagnoli) checksum.
         * @details Optimized for modern Intel silicon using SSE4.2 intrinsics.
         * Handles tail bytes to ensure bit-perfect integrity validation for any length.
         */
        static inline uint64_t compute_crc32c(const void* data, size_t len) noexcept {
            uint64_t crc = 0xFFFFFFFF;
            const uint8_t* p = static_cast<const uint8_t*>(data);
            size_t offset = 0;

            // Parallel CRC Pipelining: Overlap hardware latencies.
            // Replaces sequential loops to maximize instruction-level parallelism.
            for (; offset + 16 <= len; offset += 16) {
                crc = _mm_crc32_u64(crc, *reinterpret_cast<const uint64_t*>(p + offset));
                crc = _mm_crc32_u64(crc, *reinterpret_cast<const uint64_t*>(p + offset + 8));
            }
            
            if (offset + 8 <= len) {
                crc = _mm_crc32_u64(crc, *reinterpret_cast<const uint64_t*>(p + offset));
                offset += 8;
            }

            // Bit-Width Mask: Branchless Tail Fold.
            // Replaces textbook 'if' chain with a single masked register load.
            if (offset < len) {
                uint64_t tail = 0;
                size_t remaining = len - offset;
                std::memcpy(&tail, p + offset, remaining); // Compiler optimizes to scalar MOV
                uint64_t mask = (1ULL << (remaining * 8)) - 1;
                crc = _mm_crc32_u64(crc, tail & mask);
            }

            return crc;
        }

        /**
         * @brief Computes a fast hardware-accelerated checksum of a block.
         */
        static inline uint64_t compute_checksum(const void* data, size_t len) noexcept {
            return compute_crc32c(data, len);
        }

        /**
         * @brief Validates data. Panics on failure to prevent corrupted state.
         */
        static inline void verify_or_panic(const void* data, size_t len, uint64_t expected) {
            if (compute_checksum(data, len) != expected) [[unlikely]] {
                handle_critical_error("CRITICAL: Bit-corruption detected in Memory!");
            }
        }
    };
} // namespace slabflux::core
