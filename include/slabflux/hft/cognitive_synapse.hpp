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

#include <string_view>
#include <cstring>
#include <span>
#include "slabflux/io/uring_shim.hpp" // For uring_shim functions
#include <vector>
#include "slabflux/hw/intrinsics.hpp"
#include "slabflux/io/simd_parser.hpp"
#include "slabflux/hft/zero_copy_egress.hpp"
//#include "reaktor.cpp"

namespace slabflux::hft {

    using namespace io;

    /**
     * @brief Cognitive Synapse for reactor integration.
     */
    struct alignas(64) cognitive_synapse {
        // Ensure the synapse itself doesn't cross cache lines unnecessarily
        static_assert(sizeof(BaseEngineRunner*) <= 8, "Pointer size mismatch");

        // Explicit ownership (No hidden global state)
        BaseEngineRunner& reactor_;
        io_uring* ring_;
        int egress_fd_;
        alignas(64) prebaked_response cached_template_;

        // Actionable stimulus tokens
        static constexpr int TOKEN_ACTION_BUY = 8554;

        void initialize_template() noexcept {
            cached_template_.length = 64;
            cached_template_.patch_offset_1 = 16;

            // Pre-zero the buffer area
            std::memset(cached_template_.buffer, 0, sizeof(cached_template_.buffer));

            // Pre-fill Ethernet/IP/TCP headers here...
            // Example: Set EtherType to IPv4 (0x0800) at offset 12
            cached_template_.buffer[12] = 0x08;
            cached_template_.buffer[13] = 0x00;
        }

        /**
         * @brief Fast-path execution handler.
         */
        SLAB_FORCE_INLINE void on_fast_path([[maybe_unused]] std::string_view method, std::string_view payload) noexcept {
            // 1. INGRESS
            // Extract the stimulus from the payload. This may be a price tick or a prompt token.
            // (Based on the bench_avx2_parser logic)
            int stimulus_token = extract_token_fast(payload);
            
            // 2. REACTOR
            const int decision_token = pulse_reactor(stimulus_token);

            // 3. EGRESS
            // If the Reactor generated an action token (e.g., BUY), fire immediately.
            if (is_actionable(decision_token)) {
                prebaked_response& resp = get_trade_template();
                egress_engine::fire_response(ring_, egress_fd_, resp, decision_token);
            }
        }

    private:
        SLAB_FORCE_INLINE int extract_token_fast(std::string_view payload) const noexcept {
            // Constraint: Force L1-D residency and ensure the first 32-byte chunk 
            // is available for the vector logic.
            if (SL_EXPECT_FALSE(payload.size() < 32)) return 0;

            return static_cast<int>(simd_parser::fast_atoi_4(payload.data()));
        }

        SLAB_FORCE_INLINE int pulse_reactor(int stimulus) noexcept {
            // In reaktor.cpp, 'core.forward()' performs the CPU work,
            // and you already added the GPU calls, so we invoke one step here.

            // Connect to the reactor (the exact method depends on how you named
            // the generation function in BaseEngineRunner instead of the main loop)
            // The GPU forward call happens here.
            float* logits = reactor_.core.forward(stimulus);

            static constexpr std::span<const int> empty_mask{};
            return reactor_.sampler.sample(logits, empty_mask);
        }

        SLAB_FORCE_INLINE bool is_actionable(int token) const noexcept {
            return token == TOKEN_ACTION_BUY;
        }

        SLAB_FORCE_INLINE prebaked_response& get_trade_template() noexcept {
            return cached_template_;
        }
    };

} // namespace slabflux::hft
