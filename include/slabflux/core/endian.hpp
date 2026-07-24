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
#include <bit>
#include <immintrin.h> // For _mm_pause
#include "slabflux/core/hot_path_alignment.hpp" // For SLAB_FORCE_INLINE

#if defined(_MSC_VER)
    #include <stdlib.h>
#endif

namespace slabflux::core {

    /**
     * @brief Constexpr byte-swapping utility.
     * @details Provides a generic, compile-time evaluable byte-swapping function
     * for integral types, replacing platform-specific intrinsics.
     */
    template <typename T>
    SLAB_FORCE_INLINE static constexpr T byteswap_constexpr(T value) noexcept {
        static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool>, "byteswap_constexpr only supports integral types.");
        if constexpr (sizeof(T) == 1) {
            return value;
        } else if constexpr (sizeof(T) == 2) {
            return static_cast<T>((value << 8) | (value >> 8));
        } else if constexpr (sizeof(T) == 4) {
            return static_cast<T>(
                ((value & 0x000000FFU) << 24) |
                ((value & 0x0000FF00U) << 8)  |
                ((value & 0x00FF0000U) >> 8)  |
                ((value & 0xFF000000U) >> 24)
            );
        } else if constexpr (sizeof(T) == 8) {
            return static_cast<T>(
                ((value & 0x00000000000000FFULL) << 56) |
                ((value & 0x000000000000FF00ULL) << 40) |
                ((value & 0x0000000000FF0000ULL) << 24) |
                ((value & 0x00000000FF000000ULL) << 8)  |
                ((value & 0x000000FF00000000ULL) >> 8)  |
                ((value & 0x0000FF0000000000ULL) >> 24) |
                ((value & 0x00FF000000000000ULL) >> 40) |
                ((value & 0xFF00000000000000ULL) >> 56)
            );
        } else {
            // Fallback for unsupported sizes, or a static_assert could be used.
            return value;
        }
    }

    /**
     * @brief Zero-syscall, compiler-optimized byte order translation layer.
     */
    class endian {
    public:
        SLAB_FORCE_INLINE static constexpr uint16_t network_to_host16(uint16_t val) noexcept {
            if constexpr (std::endian::native == std::endian::little) {
                return byteswap_constexpr(val);
            }
            return val;
        }

        SLAB_FORCE_INLINE static constexpr uint16_t host_to_network16(uint16_t val) noexcept {
            return network_to_host16(val);
        }

        SLAB_FORCE_INLINE static constexpr uint32_t network_to_host32(uint32_t val) noexcept {
            if constexpr (std::endian::native == std::endian::little) {
                return byteswap_constexpr(val);
            }
            return val;
        }

        SLAB_FORCE_INLINE static constexpr uint32_t host_to_network32(uint32_t val) noexcept {
            return network_to_host32(val);
        }

        /**
         * @brief  Header Swap.
         * @details Replaces scalar bswap loops with 1-cycle AVX2 shuffling.
         * Bit-perfectly translates a 16-byte protocol window from network to host order.
         */
        SLAB_FORCE_INLINE static void translate_header_128(void* header_ptr) noexcept {
#if defined(__AVX2__)
            // A 128-bit header containing 64-bit LSN/Timestamps requires 64-bit byte-swaps.
            // The previous 16-bit mask (1,0,3,2...) would silently corrupt the payload.
            const __m128i v_swap_mask = _mm_setr_epi8(
                7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8
            );
            __m128i v_header = _mm_loadu_si128(reinterpret_cast<const __m128i*>(header_ptr));
            v_header = _mm_shuffle_epi8(v_header, v_swap_mask);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(header_ptr), v_header);
#else
            // Scalar fallback is displaced for I-Cache residency
            std::uint64_t* p = static_cast<std::uint64_t*>(header_ptr);
            p[0] = network_to_host64(p[0]);
            p[1] = network_to_host64(p[1]);
#endif
        }

        SLAB_FORCE_INLINE static constexpr uint64_t network_to_host64(uint64_t val) noexcept {
            if constexpr (std::endian::native == std::endian::little) {
                return byteswap_constexpr(val);
            }
            return val;
        }

        SLAB_FORCE_INLINE static constexpr uint64_t host_to_network64(uint64_t val) noexcept {
            return network_to_host64(val);
        }
    };

} // namespace slabflux::core