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
 * ============================================================================* @brief SLABFLUX - Labs
 */

#pragma once
#include <immintrin.h>
#include <array>
#include <optional>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/integrity_validator.hpp"

namespace slabflux::bridge {

using namespace core;

/**
 * @brief Hardware-agnostic SIMD vector wrapper.
 * @tparam Dim Dimension of the vector.
 */
template <size_t Dim>
struct alignas(64) simd_vector {
    float data[Dim];

    static constexpr size_t size() { return Dim; }
};

/**
 * @brief High-frequency signal processing backbone.
 * @tparam Dim SIMD lane width.
 * @tparam ContextSize Number of historical pulses to keep in cache.
 */
template <size_t Dim, size_t ContextSize>
class signal_backbone {
public:
    static_assert((ContextSize & (ContextSize - 1)) == 0, "ContextSize must be a power of 2.");
    alignas(64) mutable std::array<simd_vector<Dim>, ContextSize> memory_ring;
    alignas(64) mutable size_t head_index{0};

public:
    template <typename T>
    SLAB_FORCE_INLINE uint32_t compute_integrity(const T& data) noexcept {
        return static_cast<uint32_t>(integrity_validator::compute_checksum(&data, sizeof(T)));
    }

    SLAB_FORCE_INLINE void prefetch_next() const noexcept {
        _mm_prefetch(reinterpret_cast<const char*>(&memory_ring[(head_index + 1) & (ContextSize - 1)]), _MM_HINT_T0);
    }

    SLAB_FORCE_INLINE std::optional<simd_vector<Dim>> process_signal(const simd_vector<Dim>& input) const noexcept {
        memory_ring[head_index & (ContextSize - 1)] = input;
        head_index = (head_index + 1) & (ContextSize - 1);
        return input;
    }
};

} // namespace slabflux::bridge

// Compatibility alias for conduit-level integrity checks
namespace slabflux::conduit {
    using namespace core;
    using namespace bridge;

    template <typename T>
    inline uint32_t compute_integrity(const T& data) noexcept {
        return static_cast<uint32_t>(integrity_validator::compute_checksum(&data, sizeof(T)));
    }

    template <size_t N, size_t Size>
    using iron_ring_buffer = signal_backbone<Size, N>;
}
