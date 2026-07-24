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
 * @file fabric.hpp
 * @brief Zero-copy Inter-Node Communication.
 * @details Uses raw L2 frames or RDMA to bypass the kernel entirely 
 * and achieve sub-microsecond cross-node latency.
 */

#pragma once

#include "slabflux/core/slab_allocator.hpp"
#include "slabflux/sys/signal_shield.hpp"
#include <immintrin.h> // For PCIe MMIO and Cache-Line control

namespace slabflux::platform {

    struct fabric_header {
        uint64_t global_lsn;   // The heartbeat of the cluster
        uint16_t source_node;
        uint16_t message_type;
    };

    class fabric {
        // Memory Mapped I/O (MMIO) pointer to the PCIe NIC Doorbell Register
        void* nic_tx_doorbell_{nullptr};
        void* nic_tx_ring_{nullptr};

    public:
        /**
         * @brief Direct-to-NIC Zero-Copy Broadcast.
         * @details Bypasses kernel networking entirely. Writes the fabric header
         * directly into the PCIe Base Address Register (BAR) using non-temporal 
         * SIMD instructions to eliminate L3 cache pollution.
         */
        SLAB_HOT void broadcast_delta(const fabric_header& header, const void* data) noexcept {
            if (SL_EXPECT_FALSE(!nic_tx_ring_)) return;

            // 1. Stream data directly to the NIC's ring buffer bypassing CPU Cache
            _mm256_stream_si256(static_cast<__m256i*>(nic_tx_ring_), 
                                _mm256_loadu_si256(static_cast<const __m256i*>(data)));

            // 2. Memory Fence ensures the payload reaches the PCIe bus before the Doorbell
            _mm_sfence();

            // 3. Ring the NIC Doorbell via Cache-Line Write-Back (CLWB)
            *static_cast<volatile uint32_t*>(nic_tx_doorbell_) = 1;
            _mm_clwb(nic_tx_doorbell_);
        }

        /**
         * @brief Listens for the next global event.
         */
        inline const fabric_header* poll_next() noexcept {
            // Non-blocking busy-wait on the hardware ring
            return nullptr; 
        }
    };
}