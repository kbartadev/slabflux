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

#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/net/autotelic_chrysalis.hpp"
#include "slabflux/security/kinetic_inscription.hpp"

namespace slabflux::net {

    /**
     * @brief The Egress Integrity Shield Handoff
     * @details Wires the Autotelic Chrysalis directly into the Teleological 
     * Agnosia failure model for outbound network traffic. Guards the final 
     * hop between L3 cache and the NIC without stalling the transmission loop.
     */
    template <typename ConduitType, typename T, typename EgressBackend>
    class egress_shield_wiring {
    private:
        // Cache-line isolated state for the Egress (Consumer) Thread
        struct alignas(64) egress_state {
            ConduitType* compute_conduit_;
            EgressBackend* nic_backend_;
            const security::semiotic_tapestry* tapestry_{nullptr};
            uint32_t expected_lsn_{1};
        } state_;

        // The Aphasic Horizon dispatch matrix
        using agnosia_sink_t = void (*)(egress_shield_wiring*, const T&, uint8_t);
        agnosia_sink_t aphasic_horizon_[256];

        static void execute_valid_transmission(egress_shield_wiring* wiring, const T& payload, uint8_t) noexcept {
            // Data is fully intact. Blast it onto the wire.
            wiring->state_.nic_backend_->transmit(payload);
        }

        static void execute_void_transmission(egress_shield_wiring* wiring, const T&, uint8_t fray) noexcept {
            // Teleological Agnosia. The corrupted payload is starved of execution context.
            // Instead of transmitting, we permanently engrave the anomaly into the hardware.
            if (wiring->state_.tapestry_) {
                wiring->state_.tapestry_->engrave_anomaly(fray, wiring->state_.expected_lsn_);
            }
        }

    public:
        explicit egress_shield_wiring(ConduitType& conduit, EgressBackend& backend) {
            state_.compute_conduit_ = &conduit;
            state_.nic_backend_ = &backend;
            
            // Initialize the Aphasic Horizon: 0 is valid, 1-255 map to oblivion.
            aphasic_horizon_[0] = &execute_valid_transmission;
            for (int i = 1; i < 256; ++i) {
                aphasic_horizon_[i] = &execute_void_transmission;
            }
        }

        void bind_tapestry(const security::semiotic_tapestry* tapestry) noexcept {
            state_.tapestry_ = tapestry;
        }

        /**
         * @brief Standard spin-loop payload extractor for the Egress thread.
         */
        SLAB_FORCE_INLINE void process_egress_tick() noexcept {
            slabflux::net::autotelic_chrysalis<T> chrysalis_envelope;
            
            // 1. Wait-free extraction from the conduit
            if (state_.compute_conduit_->try_pop(chrysalis_envelope)) {
                
                // 2. Indexical Exhaustion check (3-cycle AVX-512 BITALG)
                uint8_t fray = chrysalis_envelope.execute_silicon_shearing(state_.expected_lsn_);
                
                // 3. Teleological Agnosia: Natively index into the terminal void if frayed.
                aphasic_horizon_[fray](this, chrysalis_envelope.raw_strand(), fray);
                
                state_.expected_lsn_++;
            }
        }
    };
}
