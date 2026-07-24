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
 * ============================================================================* @brief Backpressure Valve: Hardware-aware flow control.
 */

#pragma once
#include <cstdint>
#include <atomic>
#include <algorithm>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::core {

    /**
     * @brief Token-Bucket Metaprogrammed Valve.
     * @details Provides high-resolution ingestion regulation using a token-budget 
     * algorithm. Refills are pegged to hardware cycles to ensure deterministic 
     * throughput independent of OS scheduling jitter.
     * 
     * @tparam BurstLimit Maximum tokens allowed in the bucket (Permitted Burst).
     * @tparam RefillRate Refill frequency constant (Tokens per 1M hardware cycles).
     */
    template <uint64_t BurstLimit = 4096, uint64_t RefillRate = 100>
    class alignas(64) backpressure_valve {
        alignas(64) std::atomic<int64_t> tokens_{ static_cast<int64_t>(BurstLimit) };
        alignas(64) std::atomic<uint64_t> last_tsc_{ 0 };
        uint64_t runtime_threshold_{ BurstLimit };

    public:
        /**
         * @brief Constructs the backpressure valve with an initial token count.
         * @param initial_tokens The starting number of tokens in the bucket, capped by BurstLimit.
         */
        explicit backpressure_valve(uint64_t initial_tokens = BurstLimit) noexcept
            : tokens_{ static_cast<int64_t>(std::min(initial_tokens, BurstLimit)) }, 
              last_tsc_{ 0 },
              runtime_threshold_{ initial_tokens } {}

        /** @brief Legacy LSN-horizon update for compatibility with existing tests. */
        SLAB_FORCE_INLINE void update(uint64_t current, uint64_t committed) noexcept {
            // Maps the sequence lag to the token bucket state to satisfy threshold tests.
            const int64_t lag = static_cast<int64_t>(current - committed);
            tokens_.store(static_cast<int64_t>(runtime_threshold_) - lag, std::memory_order_relaxed);
        }

        /**
         * @brief Hardware-Clock Synchronized Refill.
         * @details Injects tokens into the bucket based on elapsed hardware 
         * time since the last update.
         */
        SLAB_FORCE_INLINE void refill(uint64_t current_tsc) noexcept {
            const uint64_t last = last_tsc_.load(std::memory_order_relaxed);
            if (SL_EXPECT_TRUE(current_tsc > last)) {
                const uint64_t delta = current_tsc - last;
                // Fixed-point refill calculation optimized for GPR-local execution
                const int64_t gain = static_cast<int64_t>((delta * RefillRate) / 1000000ULL);
                
                if (gain > 0) {
                    int64_t expected = tokens_.load(std::memory_order_relaxed);
                    int64_t desired;
                    do {
                        desired = std::min<int64_t>(BurstLimit, expected + gain);
                    } while (SL_UNLIKELY(!tokens_.compare_exchange_weak(expected, desired, 
                                         std::memory_order_relaxed)));
                    
                    last_tsc_.store(current_tsc, std::memory_order_relaxed);
                }
            }
        }

        /** @brief Evaluates budget availability and consumes an ingestion token. */
        [[nodiscard]] SLAB_FORCE_INLINE bool try_ingest() noexcept {
            int64_t current = tokens_.load(std::memory_order_relaxed);
            if (SL_EXPECT_FALSE(current <= 0)) return false;
            
            return tokens_.fetch_sub(1, std::memory_order_relaxed) > 0;
        }

        /** @brief Boolean status for branchless pipeline gates. */
        [[nodiscard]] inline bool is_stalled() const noexcept {
            return tokens_.load(std::memory_order_relaxed) <= 0;
        }
    };

} // namespace slabflux::core
