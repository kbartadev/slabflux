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

#include "../core/pinned_slab_allocator.hpp"
#include "../core/spsc_conduit.hpp"
#include "../core/timing_wheel.hpp"
#include "../rte/error_arbiter.hpp"
#include "../security/kinetic_inscription.hpp"
#include <atomic>

namespace slabflux::domain {

    // AI tensor entity — 64‑byte alignment for cache efficiency
    struct alignas(64) ai_tensor_entity {
        uint32_t next; // For the slab free-list
        std::atomic<uint8_t> ownership_mask{ 0 };
        uint32_t expert_id;
        uint32_t entity_id; // Unique identifier

        float weights[1024];
    };

    template <typename Allocator, typename Conduit, typename Wheel>
    class moe_dispatcher {
        Allocator& memory_pool_;
        Conduit& inbound_pipe_;
        Wheel& timing_wheel_;
        const security::semiotic_tapestry* tapestry_{nullptr};
        rte::error_arbiter<1024>* arbiter_{nullptr};

    public:
        moe_dispatcher(Allocator& pool, Conduit& pipe, Wheel& time)
            : memory_pool_(pool), inbound_pipe_(pipe), timing_wheel_(time) {}

        /** @brief Dynamically binds the Semiotic Tapestry for hardware-level telemetry engraving. */
        void bind_tapestry(const security::semiotic_tapestry* tapestry) noexcept { tapestry_ = tapestry; }

        /** @brief Dynamically binds the Error Arbiter for explicit deterministic telemetry. */
        void bind_error_arbiter(rte::error_arbiter<1024>* arbiter) noexcept { arbiter_ = arbiter; }

        /**
         * @brief Dispatcher Tick.
         * @details O(1) scheduling and task assignment.
         */
        SLAB_HOT void process_tick() noexcept {
            // 1. Advance the timing wheel and handle expired tasks
            uint32_t expired_id = timing_wheel_.tick();

            // The Timing Wheel returns a linked list of expired IDs
            while (SL_EXPECT_FALSE(expired_id != Wheel::END_OF_LIST)) {
                if (tapestry_) {
                    tapestry_->engrave_anomaly(0x11, expired_id); // 0x11 = TIMEOUT anomaly
                }
                if (arbiter_) {
                    arbiter_->record_error(rte::error_domain::compute, 0x1111, rte::error_severity::warning, expired_id);
                }

                // Traverse intrusive linked list using the active Slab Allocator pool
                expired_id = memory_pool_.get_by_index(expired_id)->timer_next;
            }

            // 2. Receive new requests from the Conduit (pointer-based API)
            while (auto* incoming_request = inbound_pipe_.pop()) {

                // 3. Register in the timing wheel
                // We assume entity_id was already assigned during the Ingress phase
                timing_wheel_.schedule(incoming_request->entity_id, 100); // 100ms budget

                // 4. Branchless Routing
                // expert_id 0 -> 0b0010 (GPU 1)
                // expert_id 1 -> 0b0100 (GPU 2)
                // Formula: 1 << (expert_id + 1)
                uint8_t target_mask = static_cast<uint8_t>(1 << (incoming_request->expert_id + 1));

                incoming_request->ownership_mask.store(target_mask, std::memory_order_release);
            }
        }
    };
} // namespace slabflux::domain
