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
 * @brief  SLABFLUX Chip Core
 * @details Deterministic Telemetry: Zero-overhead system monitoring.
 */

#pragma once
#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"


namespace slabflux::core {

    /**
     * @brief MSR Event Configuration.
     * @details Encapsulates hardware MSR (Model Specific Register) selection logic
     * within a C++20 consteval container.
     */
    struct msr_event {
        uint32_t id;
        consteval msr_event(uint32_t msr_id) : id(msr_id) {}
    };

    // Common MSRs (example, actual MSRs depend on CPU architecture)
    inline constexpr msr_event MSR_TSC{0x10};    // Time Stamp Counter
    inline constexpr msr_event MSR_APERF{0xE8};  // Actual Performance Counter
    inline constexpr msr_event MSR_MPERF{0xE7};  // Maximum Performance Counter

    /**
     * @brief MSR-Bound Metaprogrammed Reader.
     * @details Replaces generic MSR access boilerplate with a compile-time
     * specialized reader, ensuring the MSR ID is a constant for the `rdmsr`
     * instruction.
     * @tparam Event The consteval msr_event configuration.
     */
    template <msr_event Event>
    class msr_reader {
    public:
        SLAB_FORCE_INLINE uint64_t sample() const noexcept {
            uint32_t low, high;
            asm volatile("rdmsr" : "=a" (low), "=d" (high) : "c" (Event.id) : "memory");
            return (static_cast<uint64_t>(high) << 32) | low;
        }
    };

    /**
     * @brief Chip-level statistics block.
     * @details Structural Honesty.
     */
    struct alignas(64) chip_telemetry {
        uint64_t total_packets;
        uint64_t dropped_packets;
        uint64_t last_lsn;
        float    avg_latency_ns;

        // Direct write into shared memory (SHM)
        inline void record_arrival(uint64_t lsn) noexcept {
            total_packets++;
            last_lsn = lsn;
        }

        // Example usage:
        // msr_reader<MSR_TSC> tsc_reader;
        // uint64_t current_tsc = tsc_reader.sample();
        //
        // msr_reader<MSR_APERF> aperf_reader;
        // uint64_t current_aperf = aperf_reader.sample();
    };

} // namespace slabflux::core
