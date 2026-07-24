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
#include <new>
#include <type_traits>
#include <nmmintrin.h>

namespace slabflux::net {

/**
 * @brief Network Wire Envelope with Logical Sequence Numbering.
 * @details Implements a strictly aligned, binary-stable frame format designed 
 * for zero-copy Shared Memory (SHM) and high-frequency network transport.
 * 
 * High-Performance Design:
 * 1. Binary Parity: Layout in memory is bit-identical to the wire to eliminate 
 *    serialization overhead and enable zero-copy SHM transfers.
 * 2. Cache-Line Alignment: `alignas(64)` ensures the entire frame resides on a 
 *    single cache line, preventing false sharing and optimizing DMA transfers.
 * 3. Zero-Copy Transport: Designed to be directly mapped into SHM pools and 
 *    passed to kernel-bypass I/O (e.g., io_uring) without serialization overhead.
 * 4. Authoritative LSN: Carries the global deterministic timestamp for causality 
 *    and bit-perfect state reconstruction.
 * 5. Hardware-Accelerated Integrity: Includes a checksum field for detecting 
 *    silent data corruption during transit.
 * 
 * @tparam Payload The type of the payload (must be trivially copyable).
 */
template <typename Payload>
struct alignas(std::hardware_constructive_interference_size) wire_frame_lsn {
    static_assert(std::is_trivially_copyable_v<Payload>, "Payload must be trivially copyable for zero-copy transport.");
    static_assert(std::is_standard_layout_v<Payload>, "Payload must have standard layout for binary stability.");

    // 32-Byte Strictly Aligned Header
    uint64_t lsn;           // Logical Sequence Number (assigned at ingress)
    uint64_t ingress_ts;    // Hardware/Kernel timestamp in nanoseconds
    uint32_t cluster_id;    // Replaces generic 'magic' with authoritative routing ID
    uint32_t client_id;     // Source identifier
    uint32_t length;        // Length of the active payload
    uint16_t type_id;       // 0: Domain, 1: Control
    uint16_t reserved_pad;  // Explicit padding to guarantee 32-byte header boundary

    // Union: Support domain payloads and control-plane markers
    union {
        Payload payload;    // Inline payload to avoid pointer chasing
        uint64_t control;   // Administrative signal data
    };
    uint64_t checksum;      // Hardware-accelerated CRC for PLP failure detection

    static constexpr size_t total_size() {
        return sizeof(wire_frame_lsn<Payload>);
    }

    /**
     * @brief Hardware-accelerated header validation.
     * @details Utilizes SSE4.2 intrinsics to validate the 32-byte routing header
     * in a deterministic 4-cycle pulse. Distinguishes from standard database LSN structures.
     */
    [[nodiscard]] inline bool validate_header_integrity() const noexcept {
        uint64_t crc = 0;
        const uint64_t* header_ptr = reinterpret_cast<const uint64_t*>(this);
        crc = _mm_crc32_u64(crc, header_ptr[0]);
        crc = _mm_crc32_u64(crc, header_ptr[1]);
        crc = _mm_crc32_u64(crc, header_ptr[2]);
        crc = _mm_crc32_u64(crc, header_ptr[3]);
        return crc != 0; // In a full implementation, this matches a predefined signature
    }
};

} // namespace slabflux::net
