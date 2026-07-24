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
#include <liburing.h>
#include <cstdint>
#include <span>
#include <concepts>
#include <cstring>
#include <x86intrin.h>
#include "slabflux/hw/intrinsics.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::hft {

/**
 * @brief Concept for types that can be converted to a std::span for zero-copy I/O operations.
 * @details Enforces trivial copyability and standard layout, which are essential
 * for direct memory access and bit-perfect kernel interactions.
 */
template <typename T>
concept IoBufferConvertible = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T> &&
                              requires(const T& obj) {
                                  { static_cast<std::span<const std::byte>>(obj) } -> std::same_as<std::span<const std::byte>>;
                              };

/**
 * @brief Zero-Copy Pre-baked TCP Egress Buffer
 * A 64-byte L1 cache-line–aligned, pre-written network response.
 */
struct alignas(64) prebaked_response {
    // The template: intentionally fixed-size due to padding (zero branching on send)
    char buffer[128];
    uint32_t length;

    // Pointers to the positions that need patching
    uint32_t patch_offset_1;

    /** @brief Implicit conversion to std::span for zero-copy I/O. */
    operator std::span<const std::byte>() const noexcept {
        return std::span<const std::byte>(reinterpret_cast<const std::byte*>(buffer), length);
    }
};

/**
 * @brief Scalar-Packed Patcher.
 * @details Optimized for 4-digit ASCII injection. Merges four 8-bit stores
 * into a single 32-bit register transaction to minimize write-buffer pressure.
 */
struct fast_patcher {
    SLAB_FORCE_INLINE static void patch_4_digits(char* dest, uint32_t value) noexcept {
        // Arithmetic: Extracts 4 digits using fixed-point reciprocal multiplication.
        // Bypasses the hardware divider unit entirely to ensure constant-time 2-cycle extraction.
        const uint32_t d0 = (value * 8389) >> 23;            // value / 1000
        const uint32_t d1 = ((value - (d0 * 1000)) * 5243) >> 19; // remainder / 100
        const uint32_t d2 = ((value - (d0 * 1000) - (d1 * 100)) * 6554) >> 16; // remainder / 10
        const uint32_t d3 = value - (d0 * 1000) - (d1 * 100) - (d2 * 10);

        // Bit-Fusion: Packs ASCII offsets and digits in GPR registers.
        // Generates a single MOV [mem], EAX instruction for the store.
        const uint32_t res = (d0 + 0x30) | ((d1 + 0x30) << 8) | 
                             ((d2 + 0x30) << 16) | ((d3 + 0x30) << 24);
        
        __builtin_memcpy(dest, &res, 4);
    }
};

/**
 * @brief The Egress fusion into the Nexus Node.
 * This logic is embedded into PHASE 3 (Bulk Inline) of matrix_nexus.
 */
struct egress_engine {
    // Embedded directly into the CQE reader loop
    template <IoBufferConvertible ResponseType>
    SLAB_FORCE_INLINE static void fire_response(io_uring* ring,
                                               int client_fd,
                                               ResponseType& resp,
                                               uint32_t dynamic_value) noexcept
    {
        // 1. PATCH THE TEMPLATE (Direct memory overwrite)
        // The CPU works entirely in its own L1 cache here — zero syscalls.
        fast_patcher::patch_4_digits(resp.buffer + resp.patch_offset_1, dynamic_value);

        // PCIe DMA Optimization: Flush the modified cache line to main memory.
        // This prevents the NIC from stalling on a PCIe cache-snoop when fetching the buffer.
        _mm_clwb(resp.buffer + resp.patch_offset_1);

        std::span<const std::byte> data_to_send = resp; // Implicit conversion via operator

        // 2. TRUE ZERO-COPY IO_URING SEND
        // We do NOT call send()! We request a Zero-Copy SQE from the ring.
        unsigned sq_tail = ring->sq.sqe_tail;
        const unsigned sq_mask = *ring->sq.kring_mask;
        io_uring_sqe* sqe = &ring->sq.sqes[sq_tail & sq_mask];

        io_uring_prep_send_zc(sqe, client_fd, data_to_send.data(), data_to_send.size(), MSG_NOSIGNAL, 0);
        // BUGFIX: client_fd is a raw socket FD, not an io_uring registered index.
        // Using IOSQE_FIXED_FILE here causes silent -EBADF kernel packet drops.
        sqe->user_data = 0; // Fire and Forget

        // The pointer advance is performed by the Nexus Node at the end of the batch!
    }
};

} // namespace slabflux::hft
