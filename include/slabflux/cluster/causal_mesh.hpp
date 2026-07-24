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

#include "../core.hpp"
#include <atomic>
#include <array>
#include <new>
#include <x86intrin.h>

#include "../core/managed_data.hpp" // For managed_data

namespace slabflux::cluster {

    // ============================================================
    // CAUSAL DETERMINISM OVER THE WIRE
    // Zero-copy memory replication between Nodes.
    // No serialization; POD structures travel bit‑exactly.
    // ============================================================

    // Concept for an event payload that carries causal meta.
    // This enforces the structural contract for causal graph propagation.
    template <typename T>
    concept CausalEventPayload = requires(T p) {
        { p.causal_ts } -> std::convertible_to<uint64_t&>;
        { p.origin_node_id } -> std::convertible_to<uint16_t&>;
    };

    // The Causal Header guarantees event ordering across the network
    struct alignas(16) causal_header {
        uint64_t causal_ts;        // HFT Hybrid Logical Clock (TSC + Seq)
        uint16_t origin_node_id;   // Which physical machine sent it?
        uint16_t payload_size;     // How many bytes should the DMA pull?
        uint32_t _pad;             // Ensure 16-byte alignment for vectorization
    };

    template <typename ConduitType, size_t MaxPeers = 32>
    class causal_mesh_router {
        // Flat, L1‑friendly array for outgoing network pipes (e.g., io_uring Tx rings)
        std::array<ConduitType*, MaxPeers> peer_tx_pipes_{ nullptr };

        // Hardware-fused Hybrid Logical Clock (HLC)
        // Embeds the physical CPU cycle counter directly into the causal timeline.
        alignas(std::hardware_constructive_interference_size) std::atomic<uint64_t> last_tsc_{ 0 };
        alignas(std::hardware_constructive_interference_size) std::atomic<uint32_t> logical_seq_{ 0 };

    public:
        void bind_peer(uint16_t peer_id, ConduitType& tx_pipe) noexcept {
            if (peer_id < MaxPeers) [[likely]] {
                peer_tx_pipes_[peer_id] = &tx_pipe;
            }
        }

        // ============================================================
        // THE HOT PATH — Relentless Replication
        // ============================================================
        template <typename EventPayload>
        requires CausalEventPayload<EventPayload>
        [[gnu::always_inline]] inline void replicate(slabflux::core::managed_data<EventPayload>& ev, uint16_t target_peer) noexcept {
            if (target_peer >= MaxPeers || !peer_tx_pipes_[target_peer]) [[unlikely]] return;

            // 1. Hybrid Logical Clock Generation
            // Fuses the hardware Time Stamp Counter (__rdtsc) with a fallback sequence.
            uint64_t current_tsc = __rdtsc();
            uint64_t last = last_tsc_.load(std::memory_order_relaxed);
            uint64_t causal_token;

            if (SL_EXPECT_TRUE(current_tsc > last)) {
                last_tsc_.store(current_tsc, std::memory_order_relaxed);
                logical_seq_.store(0, std::memory_order_relaxed);
                causal_token = (current_tsc << 16);
            } else {
                uint32_t jitter = static_cast<uint32_t>(current_tsc & 0xF);
                uint32_t seq = logical_seq_.fetch_add(jitter + 1, std::memory_order_relaxed);
                causal_token = (last << 16) | ((seq ^ 0xA5A5) & 0xFFFF);
            }

            // Assign causal meta to the event payload.
            // This is the "propagation" step in the causal graph.
            ev->causal_ts = causal_token;
            ev->origin_node_id = 0; // Local Node ID (can be static or provided via env)
            ev->payload_size = static_cast<uint16_t>(sizeof(EventPayload));
            ev->_pad = 0; // Enforce structural integrity for CRC hashing

            // 2. Lock‑Free Push into the network pipe (towards io_uring Tx)
            if (peer_tx_pipes_[target_peer]->push(ev.get())) [[likely]] {
                // Successfully pushed into the pipe. io_uring will DMA‑pull the memory
                // to the NIC in the background (Zero‑Copy)!
                ev.release();
            }
            else {
                // Backpressure: NIC Tx buffer is full.
                // HFT logic decides: drop or spin‑wait.
            }
        }

        // Broadcast: When a state break must be known by every server
        template <typename EventPayload>
        requires CausalEventPayload<EventPayload>
        inline void blast_to_all(slabflux::core::pool<EventPayload>& pool, const EventPayload& state_snapshot) noexcept {
            uint64_t current_tsc = __rdtsc();
            uint64_t last = last_tsc_.load(std::memory_order_relaxed);
            uint64_t causal_token;

            if (SL_EXPECT_TRUE(current_tsc > last)) {
                last_tsc_.store(current_tsc, std::memory_order_relaxed);
                logical_seq_.store(0, std::memory_order_relaxed);
                causal_token = (current_tsc << 16);
            } else {
                uint32_t seq = logical_seq_.fetch_add(1, std::memory_order_relaxed);
                causal_token = (last << 16) | (seq & 0xFFFF);
            }

            for (size_t i = 0; i < MaxPeers; ++i) {
                if (peer_tx_pipes_[i]) {
                    // We MUST allocate as many copies from the pool as there are peers,
                    // because event_ptr (RAII) requires each pipe to own its instance.
                    auto ev = pool.template make<EventPayload>(state_snapshot);
                    if (ev) [[likely]] {
                        ev->causal_ts = causal_token;
                        ev->origin_node_id = 0;
                        ev->payload_size = static_cast<uint16_t>(sizeof(EventPayload));
                        ev->_pad = 0;
                        if (peer_tx_pipes_[i]->push(ev.get())) {
                            ev.release();
                        }
                    }
                }
            }
        }
    };

} // namespace slabflux::cluster
