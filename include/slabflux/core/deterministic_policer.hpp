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
 * @file deterministic_policer.hpp
 * @brief Ingress Rate Limiting.
 * @details Protects the O(1) core from network flooding using a 
 * hardware-aligned token bucket.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <immintrin.h> // For _mm_pause
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    class ingress_policer {
        const uint64_t tokens_per_tick_;
        const uint64_t max_burst_;
        
        alignas(64) std::atomic<uint64_t> available_tokens_{0};

    public:
        ingress_policer(uint64_t rate, uint64_t burst) 
            : tokens_per_tick_(rate), max_burst_(burst) {}

        /**
         * @brief Replenishes tokens. Called by the Clock Node.
         */
        inline void replenish() noexcept {
            uint64_t current = available_tokens_.load(std::memory_order_relaxed);
            uint64_t next = std::min(max_burst_, current + tokens_per_tick_);
            available_tokens_.store(next, std::memory_order_relaxed);
        }

        /**
         * @brief Consumes tokens. Called by the Ingress node.
         * @return true if the packet is allowed, false if it must be dropped.
         */
        inline bool allow(uint64_t cost) noexcept {
            uint64_t current = available_tokens_.load(std::memory_order_relaxed);
            for (uint32_t retries = 0; ; ++retries) {
                if (current < cost) [[unlikely]] return false;
                if (SL_EXPECT_TRUE(available_tokens_.compare_exchange_strong(current, current - cost, 
                    std::memory_order_acq_rel, std::memory_order_relaxed))) {
                    return true;
                }
                // Interconnect Cooldown: Stabilizes token consumption and reduces bus pressure during network ingress storms.
                for (uint32_t k = 0; k < (1U << (retries & 3)); ++k) _mm_pause();
            }
        }
    };
} // namespace slabflux::core