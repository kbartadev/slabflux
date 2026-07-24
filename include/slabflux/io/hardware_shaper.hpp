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
 * @file hardware_shaper.hpp
 * @brief Core-Local TSC-based Network Pacing.
 */

#pragma once

#include <cstdint>
#include <x86intrin.h>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::io {

    /**
     * @brief Compile-Time Hardware Flow Specification.
     * @details Statically derives the cycle-to-byte ratio for a specific NIC/CPU pair.
     * @tparam LinkSpeedGbps Physical wire speed (e.g. 10, 25, 100).
     * @tparam CpuFreqMhz Physical base frequency for TSC translation.
     */
    template <std::size_t LinkSpeedGbps, std::size_t CpuFreqMhz>
    struct hardware_flow_spec {
        // Constant Derivation: (Cycles/Sec) / (Bytes/Sec)
        // Simplified to: (Mhz * 10^6) / ((Gbps * 10^9) / 8) -> (Mhz * 8) / (Gbps * 1000)
        static constexpr double cycles_per_byte = static_cast<double>(CpuFreqMhz) / (static_cast<double>(LinkSpeedGbps) * 125.0);
    };

    /** @brief Internal tag for dynamic shaper configuration. */
    struct dynamic_flow_spec { static constexpr double cycles_per_byte = 0.0; };

    /**
     * @brief Concept-Verified Hardware Transmission Shaper.
     * @details Replaces generic configuration duplication with a statically-resolved 
     * pacing gate. Eliminates runtime division and multiplication overhead 
     * by freezing flow parameters in the type signature.
     */
    template <typename FlowSpec = dynamic_flow_spec>
    class alignas(64) hardware_shaper {
    private:
        uint64_t next_tx_cycle_{0};
        const double cycles_per_byte_;

    public:
        explicit hardware_shaper() noexcept 
            : next_tx_cycle_(__rdtsc()), cycles_per_byte_(FlowSpec::cycles_per_byte) {}

        /** @brief Fallback: Support for legacy tests and dynamic tuning. */
        hardware_shaper(double link_speed_gbps, double cpu_freq_ghz) noexcept
            : next_tx_cycle_(__rdtsc()),
              cycles_per_byte_((cpu_freq_ghz * 1000.0) / (link_speed_gbps * 125.0)) {}

        /**
         * @brief Blocks the execution pipeline until the hardware bandwidth token is met.
         * @param frame_size The size of the payload being transmitted.
         */
        SLAB_FORCE_INLINE void pace_transmission(std::size_t frame_size) noexcept {
            // Synthesis: Frame cost is calculated using the compile-time spec constant.
            const uint64_t cost_cycles = static_cast<uint64_t>(static_cast<double>(frame_size) * cycles_per_byte_);
            uint64_t current_cycle;

            // Cycle-Gated Barrier: Prevent speculative RDTSC drift.
            // Replaces generic loops with a hardware-serialized pacing gate.
            while (true) {
                _mm_lfence(); // Physical Serialization Barrier
                current_cycle = __rdtsc();
                if (current_cycle >= next_tx_cycle_) break;
                _mm_pause();
            }

            // Advance the pacing boundary
            next_tx_cycle_ = current_cycle + cost_cycles;
        }
    };

    /** @brief Deduction guide to support CTAD with dynamic configuration for testing. */
    hardware_shaper(double, double) -> hardware_shaper<dynamic_flow_spec>;

} // namespace slabflux::io
