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
 * @file nanoscope_bridge.hpp
 * @brief Zero-overhead Observability Bridge.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <immintrin.h> // For _mm_pause
#include <cstdlib> // For posix_memalign

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

namespace slabflux::bridge {

    struct alignas(64) telemetry_event {
        uint64_t timestamp;
        uint32_t event_id;
        uint64_t payload;

        // Explicit constructor to satisfy aggregate initialization with alignas
        telemetry_event() = default;
        telemetry_event(uint64_t ts, uint32_t id, uint64_t p) : timestamp(ts), event_id(id), payload(p) {}
    };

    class alignas(64) nanoscope_bridge {
        // Distributed Matrix: Heap-allocated to prevent stack overflows (256KB)
        telemetry_event* ring_;
        std::atomic<uint64_t> head_{0};

    public:
        nanoscope_bridge() {
            // Enforce physical alignment for hardware prefetching
            void* mem = nullptr;
            if (::posix_memalign(&mem, 64, sizeof(telemetry_event) * 4096) != 0) throw std::bad_alloc();
            ring_ = static_cast<telemetry_event*>(mem);
        }

        ~nanoscope_bridge() { free(ring_); }

        /**
         * @brief Ultra-fast non-blocking push from Compute Engine.
         */
        inline void trace(uint32_t id, uint64_t data) noexcept {
            uint64_t idx = head_.fetch_add(1, std::memory_order_relaxed) & 4095;
            // Release semantics ensure the payload is written before the index is consumed
            ring_[idx] = telemetry_event{__rdtsc(), id, data};
        }
    };
}