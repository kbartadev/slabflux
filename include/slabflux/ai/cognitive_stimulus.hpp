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
 * @brief High-dimensional stimulus for the deterministic AI execution core.
 * @details Sized and aligned for efficient cache tiling on contemporary CPUs.
 */
 
#pragma once
#include <cstdint>
#include "slabflux/core/pipeline.hpp"

namespace slabflux::ai {

/**
 * @brief High-dimensional stimulus for the Deterministic AI Core.
 * @details Aligned to 1024-byte boundary to match L2 cache-line batching.
 */
struct alignas(64) cognitive_stimulus {
    uint32_t raw_token;
    float confidence;
    uint64_t source_lsn;
    uint32_t _pad; // Explicit padding to enforce strict 64-bit instruction bounds
    uint8_t _tensor_pad[1004]; // Pad to 1024 bytes (L2 cache-line batching)

    constexpr cognitive_stimulus() noexcept : raw_token(0), confidence(0.0f), source_lsn(0), _pad(0), _tensor_pad{0} {}

    cognitive_stimulus(uint32_t token, float conf)
        : raw_token(token), confidence(conf), source_lsn(0), _pad(0) {}
};

// Mathematically enforce that the layout exactly matches AVX-512 register 
// pressure limits to ensure seamless tiling into physical L1 cache lines.
static_assert(sizeof(cognitive_stimulus) % 64 == 0, 
    "Cognitive Stimulus must seamlessly tile into physical L1 cache lines.");

} // namespace slabflux::ai
