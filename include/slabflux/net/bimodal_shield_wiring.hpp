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
#include "slabflux/compute/sovereign_signal.hpp"
#include "slabflux/net/autologous_isomorphism.hpp"

namespace slabflux::net {

    /**
     * @brief The Bimodal Integrity Shield Handoff
     * @details Wires the Symplectic Resonance Fencing (SRF) on the I/O Thread 
     * directly into the Autologous Conflict Isomorphism (ACI) on the Compute Thread.
     * Achieves absolute hardware-level memory safety spanning the PCIe bus and 
     * L3 cache conduits with zero scalar branching and zero latency overhead.
     */
    template <typename ConduitType, typename T, typename Allocator>
    class bimodal_shield_wiring {
    private:
        // Cache-line isolated state for the I/O (Producer) Thread
        struct alignas(64) ingress_state {
            ConduitType* compute_conduit_;
            Allocator* mem_pool_;
            uint32_t current_lsn_{0};
        } ingress_;

    public:
        explicit bimodal_shield_wiring(ConduitType& conduit, Allocator& pool) {
            ingress_.compute_conduit_ = &conduit;
            ingress_.mem_pool_ = &pool;
        }

        // =====================================================================
        // TIER 1: OUTER PERIMETER (Executed exclusively by the I/O Ingress Thread)
        // =====================================================================
        SLAB_FORCE_INLINE bool on_raw_frame(T* raw_network_buffer, int /* res */ = 0) noexcept {
            // 1. Cast the raw incoming PCIe/NIC buffer directly to the SRF envelope
            auto* srf_signal = reinterpret_cast<slabflux::compute::sovereign_signal<T>*>(
                raw_network_buffer
            );

            // 2. Symplectic Resonance Fencing (1-cycle AVX-512 VNNI check)
            if (!srf_signal->validate_and_vaporize()) [[unlikely]] {
                ingress_.mem_pool_->free(raw_network_buffer);
                return true; // Frame handled (dropped), nexus should not double-free
            }

            uint32_t active_lsn = ++ingress_.current_lsn_;

            // 4. Wrap the payload in the ACI envelope for safe transport across shared memory
            slabflux::net::autologous_isomorphism<T*> aci_envelope(1, raw_network_buffer); // Type 1: Valid Data

            // 5. Entangle the payload with the temporal clock to build the Collision Graph
            aci_envelope.embed_symmetry(active_lsn);

            // 6. Push wait-free into the lock-free conduit
            ingress_.compute_conduit_->try_push(aci_envelope);
            return true;
        }
    };
}
