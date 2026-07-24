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
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    /**
     * @brief Wire Envelope with Logical Sequence Numbering.
     * @details Implements a strictly packed, binary-stable frame format designed 
     * for zero-copy Shared Memory (SHM) and Journal transport. 
     * 
     * Design Invariants:
     * 1. Binary Parity: The layout in memory is bit-identical to the layout 
     *    on the wire/disk to eliminate serialization overhead.
     * 2. Explicit Padding: Eliminates pragma pack(1) to ensure hardware memory
     *    alignment invariants are strictly maintained, preventing CPU penalties.
     * 3. Authoritative LSN: Carries the global deterministic timestamp used 
     *    by the Replay Saga for bit-perfect state reconstruction.
     * 
     * @tparam T The trivially copyable payload type.
     */
    template<typename T>
    struct alignas(std::hardware_constructive_interference_size) wire_frame_lsn {
        static_assert(std::is_trivially_copyable_v<T>, "Payload must be strictly mechanical/POD");

        uint32_t cluster_id;     // Replaces legacy magic number with robust routing ID
        uint16_t type_id;        // Compile-time fixed type identifier
        uint16_t reserved_pad;   // Explicit padding to 64-bit alignment
        uint64_t lsn;            // Global deterministic timestamp
        T payload;               // Deterministic, trivially_copyable mechanical data
    };

} // namespace slabflux::core
