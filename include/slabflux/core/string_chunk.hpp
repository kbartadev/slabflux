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
#include <cstddef>
#include "slabflux/core.hpp"

namespace slabflux::core {

    /**
     * @struct string_chunk
     * @brief Physically deterministic memory atom.
     * alignas(64) eliminates False Sharing by isolating the chunk to one L1 cache line.
     */
    struct alignas(64) string_chunk {
        static constexpr uint32_t CAPACITY = 48;            // Max data per 64-byte slab
        static constexpr uint32_t END_OF_CHAIN = 0xFFFFFFFF;
        char data[CAPACITY];                                // 48 Bytes: The raw payload
        uint32_t next_chunk_idx{ END_OF_CHAIN };            // 4 Bytes: Index of next slab in pool
        uint32_t used_size{ 0 };                            // 4 Bytes: Actual bytes used in this chunk
        uint64_t _padding{ 0 };                             // 8 Bytes: Ensures exact 64-byte cache-line fill
    };

    /**
     * @struct fragmented_string
     * @brief Metadata handle embedded in business events.
     * Relocatable via indices instead of pointers.
     */
    struct fragmented_string {
        static constexpr uint32_t SSO_CAPACITY = 48;
        char sso_buffer[SSO_CAPACITY]{ 0 };
        uint32_t head_idx{ 0xFFFFFFFF };
        uint32_t total_length{ 0 };
        uint16_t sso_size{ 0 };
    };

    static_assert(sizeof(string_chunk) == 64, "string_chunk must be exactly one cache line");
}
