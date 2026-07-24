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
 * @file causal_mesh.hpp
 * @brief O(1) Causal Consistency Replicator.
 * @details Deterministic event ordering across a distributed
 * cluster using hardware-aligned Vector Clocks and a zero-allocation 
 * Out-of-Order (OoO) Parking Lot.
 */

#pragma once

#include <cstdint>
#include <atomic>
#include <array>
#include <system_error>

#include "slabflux/core/pinned_allocator_spsc.hpp"

namespace slabflux::mesh {

    /**
     * @brief The topological size of the cluster. 
     * @details Fixed at compile time to achieve zero dynamic allocations.
     */
    constexpr size_t MAX_CLUSTER_NODES = 8;

    /**
     * @brief The causal state of a node. 
     * @details Contains the highest exact sequence number seen from every node.
     */
    struct alignas(64) causal_vector {
        uint64_t sequences[MAX_CLUSTER_NODES]{0};

        inline bool operator==(const causal_vector& other) const noexcept {
            for (size_t i = 0; i < MAX_CLUSTER_NODES; ++i) {
                if (sequences[i] != other.sequences[i]) return false;
            }
            return true;
        }
    };

    /**
     * @brief The absolute zero-copy wire format.
     * @details Sent directly over the NIC via DMA. No serialization required.
     */
    template<typename T>
    struct alignas(64) wire_frame {
        uint16_t origin_node_id;
        uint64_t sequence_id;         // The absolute ID from the origin
        causal_vector causal_state;   // What the origin knew when it sent this
        T payload;                    // The actual domain data
    };

} // namespace slabflux::mesh
