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
#include <type_traits>
#include <utility>
#include <span>
#include <cstring> // For memcpy fallback
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    // THE UNIVERSAL ROOT STRATEGY
    struct root_strategy {
        // Fallback catch-all to satisfy the C++ requires-clause probe
        template <typename E, typename C>
        SLAB_FORCE_INLINE void on(E&, C&) {}
    };

    /**
     * @brief Provides a C++20 std::span-based interface for non-temporal memory writes.
     * @details Uses streaming stores to bypass CPU caches, preventing cache pollution
     * for data that is written once and not immediately re-read.
     * Zero-overhead wrapper around architecture-specific intrinsics.
     */
    class non_temporal_stream {
    public:
        /**
         * @brief Performs non-temporal writes from a source span to a destination span.
         * @tparam T The type of elements in the spans. Must be POD.
         * @param source The source span of data to write.
         * @param destination The destination span to write data to.
         */
        template <POD T>
        SLAB_FORCE_INLINE static void write(std::span<const T> source, std::span<T> destination) noexcept {
            static_assert(sizeof(T) > 0, "Cannot non-temporally write zero-sized types.");

            const size_t num_elements = std::min(source.size(), destination.size());
            if (num_elements == 0) {
                return;
            }

            const char* src_bytes = reinterpret_cast<const char*>(source.data());
            char* dst_bytes = reinterpret_cast<char*>(destination.data());
            size_t total_bytes = num_elements * sizeof(T);
            size_t offset = 0;

            // Prioritize AVX-512 for 64-byte chunks if available
#if defined(__AVX512F__)
            // GP Fault Protection: Non-temporal stores REQUIRE strict 64-byte alignment.
            if (SL_EXPECT_TRUE((reinterpret_cast<uintptr_t>(dst_bytes) & 63) == 0)) {
                for (; offset + 64 <= total_bytes; offset += 64) {
                    _mm512_stream_si512(reinterpret_cast<__m512i*>(dst_bytes + offset), _mm512_loadu_si512(reinterpret_cast<const void*>(src_bytes + offset)));
                }
            }
#endif
            // Then AVX2 for 32-byte chunks if available (as specifically requested)
#if defined(__AVX2__)
            // GP Fault Protection: Non-temporal stores REQUIRE strict 32-byte alignment.
            if (SL_EXPECT_TRUE((reinterpret_cast<uintptr_t>(dst_bytes + offset) & 31) == 0)) {
                for (; offset + 32 <= total_bytes; offset += 32) {
                    _mm256_stream_si256(reinterpret_cast<__m256i*>(dst_bytes + offset), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src_bytes + offset)));
                }
            }
#endif
            // Fallback for remaining bytes or if no SIMD intrinsics are used
            if (offset < total_bytes) {
                std::memcpy(dst_bytes + offset, src_bytes + offset, total_bytes - offset);
            }

            // Ensure all non-temporal writes are flushed to memory
            _mm_sfence();
        }

        /**
         * @brief Performs a non-temporal write of a single element.
         * @tparam T The type of the element. Must be POD.
         * @param source_val The source value.
         * @param destination_val The destination to write the value to.
         */
        template <POD T>
        SLAB_FORCE_INLINE static void write_one(const T& source_val, T& destination_val) noexcept {
            write(std::span<const T>(&source_val, 1), std::span<T>(&destination_val, 1));
        }
    };
}
