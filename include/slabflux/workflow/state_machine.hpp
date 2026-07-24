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

#include <array>
#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/telemetry/lock_free_bus.hpp"
#include "slabflux/security/kinetic_inscription.hpp"

namespace slabflux::workflow {

    template <typename StateEnum, typename Payload>
    struct saga_slot {
        StateEnum current_state;
        Payload data;
        bool is_active{false};
    };

    template <typename WorkflowDefinition, size_t MaxConcurrentWorkflows = 65536>
    class alignas(64) deterministic_saga {
        using StateEnum = typename WorkflowDefinition::state_type;
        using Payload = typename WorkflowDefinition::payload_type;

        // O(1) preallocated cache-friendly block
        std::array<saga_slot<StateEnum, Payload>, MaxConcurrentWorkflows> slots_{};
        const security::semiotic_tapestry* tapestry_{ nullptr };

        template <typename Pointee>
        static void execute_valid_transition(deterministic_saga*, saga_slot<StateEnum, Payload>& slot, const Pointee& ev, uint8_t) noexcept {
            WorkflowDefinition::transition(slot.current_state, slot.data, slot.is_active, ev);
        }

        template <typename Pointee>
        static void execute_void_transition(deterministic_saga* saga, saga_slot<StateEnum, Payload>&, const Pointee& ev, uint8_t fray) noexcept {
            // Kinetic Inscription: Engrave workflow boundary faults without allocation.
            if (saga->tapestry_) {
                saga->tapestry_->engrave_anomaly(fray, ev.workflow_id);
            }
        }

    public:
        explicit deterministic_saga() noexcept {}

        void bind_tapestry(const security::semiotic_tapestry* tapestry) noexcept {
            tapestry_ = tapestry;
        }

        template <typename Event>
        SLAB_FORCE_INLINE void on(const Event& ev) noexcept {
            if (!ev) return;

            // 1. O(1) lookup: the event tells us which workflow it belongs to
            const uint32_t id = ev->workflow_id;

            // Correctly deduce the type behind the pointer to avoid signature mismatches
            using Pointee = std::remove_reference_t<decltype(*ev)>;
            using sink_t = void (*)(deterministic_saga*, saga_slot<StateEnum, Payload>&, const Pointee&, uint8_t);
            const sink_t aphasic_horizon_[2] = { &execute_valid_transition<Pointee>, &execute_void_transition<Pointee> };

            // Evaluate the Indexical Fray
            uint8_t fray = (id >= MaxConcurrentWorkflows) ? 1 : 0;

            // Teleological Agnosia: Natively index into the terminal void if frayed.
            aphasic_horizon_[fray](this, slots_[id & (MaxConcurrentWorkflows - 1)], *ev, fray);
        }
    };
}
