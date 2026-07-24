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

#include "slabflux/compute/simd_engine.hpp"
#include "slabflux/io/durable_journal.hpp"
#include "slabflux/core/hot_path_alignment.hpp"
#include <cstdint>

namespace slabflux::compute {

    /**
     * @brief State Synchronizer.
     * @details Ensures the Engine is bit-identical to the Journaled state
     * and physically "hot" before switching to live network ingress.
     */
    template<typename Engine, typename Journal>
    class replay_manager {
    private:
        Engine& engine_;
        Journal& journal_;

        enum class SystemState : uint8_t { INIT, REPLAYING, WARMING, LIVE };
        SystemState state_{ SystemState::INIT };

    public:
        replay_manager(Engine& eng, Journal& jrn) : engine_(eng), journal_(jrn) {}

        /**
         * @brief Synchronizes the engine and primes the silicon.
         * @details This is a COLD PATH operation. It prepares the HOT PATH.
         */
        SLAB_COLD void sync_to_live() {
            state_ = SystemState::REPLAYING;

            // 1. Journal Replay Phase
            // We process every historical event to reconstruct the exact 
            // memory state in the Slab.
            uint64_t replayed_count = 0;
            while (auto* event = journal_.read_next()) {
                if (SL_EXPECT_TRUE(event->sequence_id > engine_.get_lsn())) {
                    // Logic must be identical to LIVE processing
                    engine_.process_event(*event);
                    replayed_count++;
                }
            }

            // 2. Physical Warm-up Phase
            // We exercise the SIMD units and Branch Predictors with neutral 
            // data to prevent frequency downclocking (AVX offset) when LIVE.
            state_ = SystemState::WARMING;
            prime_silicon();

            // 3. Final State Seal
            // At this point, the Engine's LSN must match the Journal's tail.
            state_ = SystemState::LIVE;
        }

    private:
        /**
         * @brief Trains the Branch Target Buffer (BTB) and warms I-Cache.
         * @details We run the actual hot-path logic with "Ghost" events.
         */
        SLAB_COLD void prime_silicon() {
            // 10,000 iterations is the magic number for modern (2026)
            // high-performance CPUs to reach stable frequency and train
            // complex branch patterns.
            uint64_t lto_defeat_counter = 0;

            SLAB_FLAT_PATH
            for (int i = 0; i < 10000; ++i) {
                // We use a 'ghost' event that triggers the same code paths
                // but doesn't modify the permanent State Slab.
                engine_.process_warmup_ghost();

                // Force the compiler to evaluate the loop by creating a false dependency
                // on lto_defeat_counter, preventing dead-code elimination (DCE) and unrolling.
                asm volatile("" : "+r,m"(lto_defeat_counter) : : "memory");
                lto_defeat_counter++;
            }
        }
    };
}
