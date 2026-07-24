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
#include <immintrin.h>
#include <bit>
#include <cstdint>
#include <vector>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::net {

/**
 * @brief SIMD-accelerated delta compression.
 */
struct delta_block {
    uint32_t offset;
    uint8_t data[64];
};

class delta_compressor {
public:
    /**
     * @brief Identifies changed memory segments using AVX-512.
     */
    template <core::POD T>
    SLAB_HOT static size_t generate_delta(const T* current, const T* previous, 
                                 size_t count, delta_block* out_deltas) {
        static_assert(sizeof(T) % 64 == 0, "Delta compression requires payloads to be a multiple of 64 bytes to prevent silent tail-data loss.");
        
        size_t delta_count = 0;
        const size_t total_bytes = count * sizeof(T);
        const uint8_t* curr_ptr = reinterpret_cast<const uint8_t*>(current);
        const uint8_t* prev_ptr = reinterpret_cast<const uint8_t*>(previous);
        
        size_t offset = 0;
        const __m512i v_all_ones = _mm512_set1_epi64(-1LL);

        for (; offset + 128 <= total_bytes; offset += 128) {
            // Use ALIGNED loads (_mm512_load_si512) instead of unaligned (_loadu_).
            // SLABFLUX enforces alignas(64) globally; relying on unaligned loads wastes silicon bandwidth.
            __m512i v0 = _mm512_load_si512(curr_ptr + offset);
            __m512i p0 = _mm512_load_si512(prev_ptr + offset);
            __m512i v1 = _mm512_load_si512(curr_ptr + offset + 64);
            __m512i p1 = _mm512_load_si512(prev_ptr + offset + 64);

            if (SL_EXPECT_FALSE(_mm512_test_epi64_mask(_mm512_xor_si512(v0, p0), v_all_ones))) {
                out_deltas[delta_count].offset = static_cast<uint32_t>(offset / sizeof(T));
                _mm512_storeu_si512(out_deltas[delta_count++].data, v0);
            }
            if (SL_EXPECT_FALSE(_mm512_test_epi64_mask(_mm512_xor_si512(v1, p1), v_all_ones))) {
                out_deltas[delta_count].offset = static_cast<uint32_t>((offset + 64) / sizeof(T));
                _mm512_storeu_si512(out_deltas[delta_count++].data, v1);
            }
        }

        if (offset + 64 <= total_bytes) {
            __m512i v_curr = _mm512_load_si512(curr_ptr + offset);
            __m512i v_prev = _mm512_load_si512(prev_ptr + offset);
            
            if (SL_EXPECT_FALSE(_mm512_test_epi64_mask(_mm512_xor_si512(v_curr, v_prev), v_all_ones))) {
                out_deltas[delta_count].offset = static_cast<uint32_t>(offset / sizeof(T));
                _mm512_storeu_si512(out_deltas[delta_count++].data, v_curr);
            }
        }
        return delta_count;
    }
};

} // namespace slabflux::net