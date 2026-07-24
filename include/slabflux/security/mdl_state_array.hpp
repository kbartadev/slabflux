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
#include <cstdint>
#include <cstddef>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::security {

    template <typename T, std::size_t Capacity>
    class alignas(64) minkowski_state_matrix;

    template <std::size_t Capacity>
    class alignas(64) minkowski_state_matrix<float, Capacity> {
        static_assert((Capacity % 8) == 0, "Matrix capacity must be a multiple of 8 floats");

    public:
        // Exported for the Parity Verifier
        static constexpr std::size_t lattice_capacity = Capacity;

    private:
        alignas(64) float data_matrix_[Capacity];
        alignas(64) float parity_matrix_[Capacity];
        __m256 spatial_tensor_;

    public:
        explicit minkowski_state_matrix(float baseline_clock) noexcept {
            spatial_tensor_ = _mm256_set1_ps(baseline_clock * 3.14159f); 
            for (std::size_t i = 0; i < Capacity; i += 8) {
                _mm256_store_ps(&data_matrix_[i], _mm256_setzero_ps());
                _mm256_store_ps(&parity_matrix_[i], _mm256_setzero_ps());
            }
        }

        SLAB_FORCE_INLINE void write_vector(std::size_t index, __m256 v_data) noexcept {
            __m256 v_kinetic = _mm256_mul_ps(v_data, spatial_tensor_);
            __m256 v_parity = _mm256_sub_ps(_mm256_setzero_ps(), v_kinetic);

            _mm256_store_ps(&data_matrix_[index], v_data);
            _mm256_store_ps(&parity_matrix_[index], v_parity);
        }

        SLAB_FORCE_INLINE __m256 read_vector(std::size_t index) const noexcept {
            __m256 v_data   = _mm256_load_ps(&data_matrix_[index]);
            __m256 v_parity = _mm256_load_ps(&parity_matrix_[index]);

            __m256 v_s2 = _mm256_fmadd_ps(v_data, spatial_tensor_, v_parity);
            __mmask8 valid_mask = _mm256_cmp_ps_mask(v_s2, _mm256_setzero_ps(), _CMP_EQ_OQ);

            __m256i safe_data = _mm256_maskz_mov_epi32(valid_mask, _mm256_castps_si256(v_data));
            return _mm256_castsi256_ps(safe_data);
        }
    };

} // namespace slabflux::security