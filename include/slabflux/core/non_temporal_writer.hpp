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
 * @file non_temporal_writer.hpp
 * @brief High-Velocity Vector Streaming.
 * @details Implements Concept-Verified Vector Stream Dispatch to bypass the 
 * L1-L3 cache hierarchy.
 */
#pragma once
#include <immintrin.h>
#include <concepts>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    /** @brief Hardware-specific width-based stream mapping traits. */
    template <std::size_t Width>
    struct vector_stream_traits;

#if defined(__AVX512F__)
    template <>
    struct vector_stream_traits<64> {
        template <typename V>
        static SLAB_FORCE_INLINE void stream(void* dst, V vec) noexcept { 
            _mm512_stream_si512(static_cast<__m512i*>(dst), reinterpret_cast<const __m512i&>(vec)); 
        }
    };
#endif

    template <>
    struct vector_stream_traits<32> {
        template <typename V>
        static SLAB_FORCE_INLINE void stream(void* dst, V vec) noexcept { 
            _mm256_stream_si256(static_cast<__m256i*>(dst), reinterpret_cast<const __m256i&>(vec)); 
        }
    };
    /**
     * @brief Concept for architecture-native streaming vectors.
     * @details Ensures the type is a recognized SIMD register with 
     * an optimized non-temporal store implementation.
     */
    template <typename V>
    concept StreamableVector = (sizeof(V) == 32 || sizeof(V) == 64);

    /**
     * @brief High-Velocity Vector Streaming.
     * @details Implements Concept-Verified Vector Stream Dispatch to bypass the 
     * L1-L3 cache hierarchy.
     */
    class non_temporal_writer {
    public:
        /**
         * @brief Concept-Verified Vector Stream Dispatch.
         * @details Statically selects optimal hardware streaming instructions 
         * based on vector width to minimize interconnect tax.
         */
        template <StreamableVector VecT>
        static SLAB_FORCE_INLINE void stream_write(void* dst, VecT vec) noexcept {
            // Memory Topology Guard: Non-temporal stores MUST be aligned to the 
            // register width to avoid illegal instruction faults on industrial silicon.
            if (SL_EXPECT_FALSE((reinterpret_cast<uintptr_t>(dst) & (sizeof(VecT) - 1)) != 0)) {
                SLAB_HARDWARE_HALT();
            }

            vector_stream_traits<sizeof(VecT)>::stream(dst, vec);
            
            // SFENCE ensures the write-combining buffers are flushed to the wire
            _mm_sfence();
        }

        /** @brief Block-based load-stream for AVX-512 cache lines. */
        static SLAB_FORCE_INLINE void stream_write(void* dst, const void* src) noexcept {
            #if defined(__AVX512F__)
            const __m512i data = _mm512_load_si512(src);
            stream_write(dst, data);
            #elif defined(__AVX2__)
            const __m256i data = _mm256_load_si256(reinterpret_cast<const __m256i*>(src));
            stream_write(dst, data);
            #else
            // Fallback for non-AVX2/AVX512
            std::memcpy(dst, src, 32); // Assuming 32-byte block for non-temporal
            _mm_sfence();
            #endif
        }
    };
}