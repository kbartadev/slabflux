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

#include <atomic>
#include <thread>
#include <x86intrin.h>

#include "slabflux/io/shm_duplex.hpp"
#include "slabflux/io/durable_journal.hpp"
#include "slabflux/workflow/state_machine.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/transport/wire_protocol.hpp"

namespace slabflux::reactor {

    // Global execution barrier for clean shutdowns
    alignas(64) std::atomic<bool> system_running{true};

    using EventFrame = transport::raw_tcp_frame;
    using TokenConduit = core::spsc_conduit<core::tagged_pointer, 2048>;

    // ============================================================================
    // PROCESS A: THE GATEWAY NODE
    // Responsibility: Network Ingress -> NVMe Journal -> IPC Forwarding
    // ============================================================================
    // IN: slabflux/reactor/reactor.hpp
    void run_gateway_node() {
        // 1. Initialize the deterministic journal on the NVMe drive
        io::durable_journal<EventFrame> audit_log("/mnt/nvme/slabflux_audit.log", 2);

        // 2. Initialize the IPC Bridge. Pass the Journal's Arena Base for Zero-Copy offset calculation!
        io::shm_arena_duplex<2048> ipc_bridge("slabflux_ring", true, audit_log.get_arena_base());

        TokenConduit ingress_to_ipc_conduit;

        while (system_running.load(std::memory_order_relaxed)) {
            // STEP 1: Reserve physical memory on the NVMe map FIRST
            EventFrame* target_slot = audit_log.reserve_slot();

            if (__builtin_expect(target_slot != nullptr, 1)) [[likely]] {

                // STEP 2: Hardware NIC (DPDK/io_uring) writes DIRECTLY into target_slot
                uint32_t polled = hardware_nic::rx_single(target_slot);

                if (polled > 0) {
                    // STEP 3: Commit to disk asynchronously
                    audit_log.commit_slot();

                    // STEP 4: Push the absolute pointer into the internal conduit
                    auto token = core::tagged_pointer::pack(target_slot->connection_id, target_slot);

                    uint32_t spin_counter = 0;
                    while (!ingress_to_ipc_conduit.push(token)) [[unlikely]] {
                        _mm_pause(); // L1/L2 cache barát várakozás

                        if (++spin_counter > 100000) {
                            // CATASTROPHIC BACKPRESSURE: A Brain Node leállt vagy befagyott.
                            // Itt riaszthatjuk az Error Arbitert, de a memóriát nem szivárogtatjuk el.
                            labs::cluster::global_node_state.store(labs::cluster::NodeState::SAFE_MODE, std::memory_order_release);
                            break;
                        }
                    }
                }
            } else {
                // Journal exhaustion
                labs::cluster::global_node_state.store(labs::cluster::NodeState::SAFE_MODE);
            }

            // STEP 5: Flash across SHM boundary (Zero-Copy Offset Translation)
            ipc_bridge.process_egress_burst(ingress_to_ipc_conduit);

            _mm_pause();
        }
    }

    // ============================================================================
    // PROCESS B: THE BRAIN NODE (BUSINESS LOGIC)
    // Responsibility: IPC Ingestion -> O(1) State Machine -> IPC Egress
    // ============================================================================

    // Dummy definition for the state machine integration
    struct TradingSagaDef {
        using state_type = uint32_t;
        using payload_type = uint64_t;
        static inline void transition(state_type& s, payload_type& p, const EventFrame* ev) noexcept {
            // O(1) Business logic executes here
            s = ev->connection_id;
        }
    };

    void run_brain_node() {
        // 1. Initialize the IPC Bridge as the SECONDARY node (attaches to existing SHM)
        io::shm_duplex<2048> ipc_bridge("slabflux_ring", false);

        // 2. The O(1) Workflow Engine
        workflow::deterministic_saga<TradingSagaDef, 65536> logic_engine;

        // 3. Internal routing conduits
        TokenConduit shm_to_logic_conduit;
        TokenConduit logic_to_shm_conduit;

        alignas(64) core::tagged_pointer incoming_tokens[32];

        while (system_running.load(std::memory_order_relaxed)) {
            // STEP 1: Harvest incoming zero-copy events from the Gateway
            ipc_bridge.process_ingress_burst(shm_to_logic_conduit);

            // STEP 2: Drain the internal conduit and feed the state machine
            uint32_t active_events = shm_to_logic_conduit.pop_batch(incoming_tokens, 32);

            for (uint32_t i = 0; i < active_events; ++i) {
                auto* ev = reinterpret_cast<EventFrame*>(incoming_tokens[i].get_address());

                // Fire the deterministic business logic natively
                logic_engine.on(ev);

                // If the logic generates a response, queue it for egress back to the Gateway
                // logic_to_shm_conduit.push(response_token);
            }

            // STEP 3: Flush business logic responses back across the SHM boundary
            ipc_bridge.process_egress_burst(logic_to_shm_conduit);

            _mm_pause();
        }
    }
}
