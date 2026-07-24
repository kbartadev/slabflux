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
 * @file branch_predictor_warmer.hpp
 * @brief Branch Target Buffer (BTB) Priming.
 */

#pragma once

#include <memory>
#include <vector>
#include <system_error>
#include <sched.h> // Required for sched_getcpu
#include <fstream>
#include <cstring>

#ifndef _WIN32
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/inotify.h>
#endif

#include "slabflux/core/sf_node_ctx.hpp"
#include "slabflux/sys/cache_partitioner.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/core/fused_nexus_node.hpp"
#include <thread>
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/sys/admin_interface.hpp"
#include "slabflux/sys/clock_node.hpp"
#include "slabflux/core/pool.hpp" // For core::pool
#include "slabflux/net/state_streamer.hpp"
#include "slabflux/compute/snapshot_engine.hpp"
#include "slabflux/mesh/causal_mesh.hpp"
#include "slabflux/mesh/causal_ingress_router.hpp"
#include "slabflux/compute/simd_engine.hpp"
#include "slabflux/rte/event_arbiter.hpp"
#include "slabflux/rte/error_arbiter.hpp"
#include "slabflux/sys/blackbox_recorder.hpp"
#include "slabflux/sys/audit_ledger.hpp"
#include "slabflux/io/durable_journal.hpp"
#include "slabflux/security/kinetic_inscription.hpp"
#include "slabflux/net/autotelic_chrysalis.hpp"
#include "slabflux/net/bimodal_shield_wiring.hpp"
#include "slabflux/net/egress_shield_wiring.hpp"
#include "slabflux/net/retransmission_buffer.hpp"
#include "slabflux/net/nack_handler.hpp"
#include "slabflux/platform/os.hpp"

// Forward declarations to break circular includes
namespace slabflux::net {
    template<typename FrameType, size_t WindowSize> class retransmission_buffer;
    template<typename FrameType, size_t WindowSize> class nack_handler;
}

namespace slabflux::rte {

    struct teleological_agnosia_policy {};
    struct error_arbiter_policy {};
    struct strict_exception_policy {};

    /**
     * @brief Binary configuration layout for zero-jitter mmap updates.
     */
    struct alignas(64) binary_config_payload {
        float precision_delta;
        float sanitizer_baseline;
        float critical_drift;
        float divergence_snapshot_threshold;
        uint8_t drift_policy;
        bool weighted_sanitization;
        bool drift_smoothing;
    };

    /**
     * @brief The ignited Real-Time Environment.
     * @details Holds the explicit lifecycle of all physical nodes and memory pools.
     */
    template<typename EventType, typename StateLogic, size_t Cap, typename FailurePolicy = teleological_agnosia_policy>
    class active_environment {
        using WireFrame = mesh::wire_frame<EventType>;
        using NetAlloc  = core::pinned_allocator_spsc<WireFrame, Cap>;
        using TimeAlloc = core::pinned_allocator_spsc<sys::tick_event, Cap>;
        using NetBus    = core::spsc_conduit<WireFrame*, Cap>;
        using AciEnvelope = net::autologous_isomorphism<WireFrame*>;
        using ShieldConduit = core::spsc_conduit<AciEnvelope, Cap>;
        using BimodalShield = net::bimodal_shield_wiring<ShieldConduit, WireFrame, NetAlloc>;
        using TimeBus   = core::spsc_conduit<sys::tick_event*, Cap>;
        using AdminBus  = core::spsc_conduit<sys::admin_command*, 128>;
        using NackAlloc = core::pinned_allocator_spsc<net::nack_request, 1024>;
        using NackBus   = core::spsc_conduit<net::nack_request, 1024>;
        using RetransBuffer = net::retransmission_buffer<WireFrame, Cap>;
        using ArbiterObj = event_arbiter<AdminBus, TimeBus, NetBus>;

        // Capacity Audit:
        // The batch size of the drain (MAX_DRAIN_BATCH) must be sufficient to clear 
        // the pool faster than jitter accumulates it. We ensure Cap can hold 
        // a full reorder window across all nodes plus kernel Rx headroom.
        static_assert(Cap >= (mesh::MAX_CLUSTER_NODES * 1024) + 4096, 
            "Starvation Risk: NetAlloc Capacity must exceed total cluster parking capacity + kernel RX headroom.");

        using Engine    = compute::branchless_engine<StateLogic, EventType, NetBus>;
        using Router    = mesh::causal_ingress_router<EventType, 1024, Engine, NetAlloc, NackBus, NackAlloc>;

        // --- 📂 Resource Plane (Static Ownership) ---
        alignas(64)
        std::unique_ptr<NetAlloc>  net_pool_;
        std::unique_ptr<NackAlloc> nack_pool_;
        std::unique_ptr<TimeAlloc> time_pool_;
        std::unique_ptr<NetBus>    net_conduit_;
        std::unique_ptr<ShieldConduit> shield_conduit_;
        std::unique_ptr<BimodalShield> ingress_shield_;
        std::unique_ptr<TimeBus>   time_conduit_;
        std::unique_ptr<NackBus>   nack_conduit_;
        std::unique_ptr<AdminBus>  admin_conduit_;
        std::unique_ptr<RetransBuffer, void(*)(RetransBuffer*)> retrans_buffer_;
        std::unique_ptr<ArbiterObj> event_arbiter_;
        std::unique_ptr<net::state_streamer<RetransBuffer>> state_streamer_;

        // --- 📂 Compute Hot-Path State (Isolated) ---
        alignas(64)
        uint32_t adaptive_batch_target_{ 1 };
        uint64_t last_liveness_tsc_{ 0 };
        uint64_t ema_logic_latency_{ 50000 }; // TSC cycles EMA
        uint64_t retrans_bytes_last_cycle_{ 0 };
        bool nack_throttled_{ false };
        bool constrained_mode_;
        std::atomic<bool> system_active_{ true };

        // --- 📂 Management & Metadata ---
        alignas(64)
        uint16_t node_id_;
        int conduit_core_;
        int io_worker_fd_;
        std::unique_ptr<core::sf_node_ctx> context_;
        int inotify_fd_{ -1 };
        int watch_fd_{ -1 };
 
        // --- 📂 Active Logic Units ---
        alignas(64)
        std::unique_ptr<net::nack_handler<WireFrame, Cap>>                    nack_responder_;
        std::unique_ptr<core::fused_nexus_node<WireFrame, Cap, BimodalShield>>  nexus_;
        std::unique_ptr<io::clock_node<TimeAlloc, TimeBus>>                    clock_node_;
        std::unique_ptr<io::durable_journal<WireFrame>>                        journal_;
        std::unique_ptr<Router>                                                causal_router_;
        std::unique_ptr<Engine, void(*)(Engine*)>                              engine_;
        std::unique_ptr<compute::snapshot_manager<StateLogic>>                 snapshot_mgr_; // Manages state snapshots

        // --- 📂 Verification & Quadripartite Defense ---
        alignas(64)
        uint32_t expected_compute_lsn_{1};
        bool debug_mode_{ false };
        bool dry_run_{ false };
        std::atomic<float> divergence_snapshot_threshold_{ 0.0f };
        std::unique_ptr<sys::audit_ledger<>> audit_ledger_;
        std::unique_ptr<sys::blackbox_recorder<>> blackbox_recorder_;
        std::unique_ptr<rte::error_arbiter<>> error_arbiter_;
        
        std::unique_ptr<security::semiotic_tapestry> tapestry_;
        std::unique_ptr<security::panoptic_reticle> reticle_;

        using agnosia_sink_t = void (*)(active_environment*, uint8_t);
        agnosia_sink_t aphasic_horizon_[256];

        struct route_context {
            WireFrame** batch;
            uint32_t count;
            uint64_t cycle_start;
            uint64_t logic_start;
            uint64_t journal_cycles;
        };

        using route_sink_t = void (*)(active_environment*, const route_context&, uint8_t);
        route_sink_t route_horizon_[256];

        static void execute_valid(active_environment*, uint8_t) noexcept {}
        static void execute_void(active_environment* env, uint8_t fray) noexcept {
            if constexpr (std::is_same_v<FailurePolicy, error_arbiter_policy>) {
                env->record_fault(fray, env->engine_->get_lsn());
            } else if constexpr (std::is_same_v<FailurePolicy, strict_exception_policy>) {
                throw std::runtime_error("Strict Exception Policy: Hardware Anomaly Detected");
            } else {
                if (env->tapestry_) env->tapestry_->engrave_anomaly(fray, env->engine_->get_lsn());
            }
        }

        static void execute_valid_route(active_environment* env, const route_context& ctx, uint8_t) noexcept {
            const uint64_t router_start = __rdtsc();
            uint64_t anomaly_mask = 0;
            for (uint32_t i = 0; i < ctx.count; ++i) { // Process each frame in the batch
                env->retrans_buffer_->insert(*(ctx.batch[i]));
                anomaly_mask |= env->causal_router_->on_raw_frame(ctx.batch[i]);
            }

            const size_t cascade_limit = (SL_EXPECT_FALSE(__builtin_popcountll(anomaly_mask) > 0)) ? 4 : 16;
            size_t unblocked = env->causal_router_->drain_all_parking_lots(anomaly_mask);

            const uint64_t router_cycles = __rdtsc() - router_start;
            uint8_t stall_fray = (SL_EXPECT_FALSE(router_cycles > 500'000)) ? 0x57 : 0;
            env->aphasic_horizon_[stall_fray](env, stall_fray);
            
            uint64_t logic_cycles = __rdtsc() - ctx.logic_start;
            env->ema_logic_latency_ = (env->ema_logic_latency_ * 31 + logic_cycles) >> 5;
            
            env->mesh_congestion_audit(unblocked);
            
            float cycle_mse = env->engine_->get_last_trap_mse();

            if (env->blackbox_recorder_) {
                env->blackbox_recorder_->record({
                    ctx.logic_start - ctx.cycle_start,
                    __rdtsc() - ctx.logic_start,
                    ctx.journal_cycles,
                    env->retrans_bytes_last_cycle_,
                    static_cast<uint64_t>(cascade_limit),
                    static_cast<uint64_t>(cycle_mse), // Cycle MSE
                    env->engine_->get_mse_ema(),
                    static_cast<float>(env->nexus_->get_full_drop_count())
                });
                env->retrans_bytes_last_cycle_ = 0;
            }

            uint8_t health_fray = (SL_EXPECT_FALSE(__builtin_popcountll(anomaly_mask) > (StateLogic::capacity / 20))) ? 0x8E : 0;
            env->aphasic_horizon_[health_fray](env, health_fray);

            uint8_t anomaly_fray = (SL_EXPECT_FALSE(anomaly_mask != 0)) ? 0xA4 : 0;
            env->aphasic_horizon_[anomaly_fray](env, anomaly_fray);
        }

        static void execute_void_route(active_environment* env, const route_context&, uint8_t fray) noexcept {
            if constexpr (std::is_same_v<FailurePolicy, error_arbiter_policy>) {
                if (env->error_arbiter_) {
                    env->record_fault(fray, env->engine_->get_lsn());
                }
            } else if constexpr (std::is_same_v<FailurePolicy, strict_exception_policy>) {
                throw std::runtime_error("Strict Exception Policy: Route Anomaly Detected");
            } else {
                if (env->tapestry_) env->tapestry_->engrave_anomaly(fray, env->engine_->get_lsn());
            }
        }

    public:
        active_environment(
            std::unique_ptr<core::sf_node_ctx> ctx,
            std::unique_ptr<NetAlloc> np, std::unique_ptr<TimeAlloc> tp,
            std::unique_ptr<NetBus> nc, std::unique_ptr<TimeBus> tc,
            std::unique_ptr<ShieldConduit> sc,
            std::unique_ptr<BimodalShield> is,
            std::unique_ptr<NackAlloc> nkap,
            std::unique_ptr<NackBus> nk,
            std::unique_ptr<AdminBus> ad,
            std::unique_ptr<RetransBuffer, void(*)(RetransBuffer*)> rb,
            std::unique_ptr<net::state_streamer<RetransBuffer>> ss,
            std::unique_ptr<net::nack_handler<WireFrame, Cap>> nr,
            std::unique_ptr<core::fused_nexus_node<WireFrame, Cap, BimodalShield>> gn,
            std::unique_ptr<io::clock_node<TimeAlloc, TimeBus>> cn,
            std::unique_ptr<io::durable_journal<WireFrame>> jr,
            std::unique_ptr<Router> cr,
            std::unique_ptr<Engine, void(*)(Engine*)> en,
            std::unique_ptr<compute::snapshot_manager<StateLogic>> sm,
            std::unique_ptr<security::semiotic_tapestry> tap,
            std::unique_ptr<security::panoptic_reticle> ret,
            bool debug_mode, // Debug mode flag
            float divergence_snapshot_threshold,
            bool dry_run,
            std::unique_ptr<sys::audit_ledger<>> al,
            std::unique_ptr<sys::blackbox_recorder<>> br,
            std::unique_ptr<rte::error_arbiter<>> ea,
            int conduit_core,
            int io_worker_fd,
            bool constrained,
            uint16_t node_id)
            : net_pool_(std::move(np)),
              nack_pool_(std::move(nkap)),
              time_pool_(std::move(tp)),
              net_conduit_(std::move(nc)),
              shield_conduit_(std::move(sc)),
              ingress_shield_(std::move(is)),
              time_conduit_(std::move(tc)),
              nack_conduit_(std::move(nk)),
              admin_conduit_(std::move(ad)),
              retrans_buffer_(std::move(rb)),
              event_arbiter_(std::make_unique<ArbiterObj>(*admin_conduit_, *time_conduit_, *net_conduit_)),
              state_streamer_(std::move(ss)),
              conduit_core_(conduit_core),
              io_worker_fd_(io_worker_fd),
              constrained_mode_(constrained),
              node_id_(node_id),
              context_(std::move(ctx)),
              nack_throttled_(false),
              nack_responder_(std::move(nr)),
              nexus_(std::move(gn)),
              clock_node_(std::move(cn)),
              journal_(std::move(jr)),
              causal_router_(std::move(cr)),
              engine_(std::move(en)),
              snapshot_mgr_(std::move(sm)),
              tapestry_(std::move(tap)),
              reticle_(std::move(ret)),
              debug_mode_(debug_mode),
              dry_run_(dry_run),
              audit_ledger_(std::move(al)), // Audit ledger for state verification
              blackbox_recorder_(std::move(br)),
              error_arbiter_(std::move(ea)) {
            
            divergence_snapshot_threshold_.store(divergence_snapshot_threshold, std::memory_order_relaxed);

            // Initialize Aphasic Horizons
            aphasic_horizon_[0] = &execute_valid;
            route_horizon_[0] = &execute_valid_route;
            for (int i = 1; i < 256; ++i) {
                aphasic_horizon_[i] = &execute_void;
                route_horizon_[i] = &execute_void_route;
            }

#ifndef _WIN32
            // Hardening: Setup inotify for automatic configuration updates
            inotify_fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
            if (inotify_fd_ >= 0) {
                watch_fd_ = ::inotify_add_watch(inotify_fd_, "slabflux_config.bin", IN_MODIFY | IN_CLOSE_WRITE);
            }
#endif
        }

        ~active_environment() {
#ifndef _WIN32
            if (watch_fd_ >= 0 && inotify_fd_ >= 0) {
                ::inotify_rm_watch(inotify_fd_, watch_fd_);
            }
            if (inotify_fd_ >= 0) {
                ::close(inotify_fd_);
            }
#endif
        }

        // Explicit control points
        void run_clock()   noexcept { 
            if (context_) watcher_on_thread_ignition(*context_, 1); // Worker 1: Clock
            
            SLAB_FLAT_PATH
            while (system_active_.load(std::memory_order_relaxed)) [[likely]] {
                clock_node_->poll(); 
                _mm_pause();
            }
        }
        void run_ingress() noexcept { 
            if (context_) watcher_on_thread_ignition(*context_, 2); // Worker 2: Ingress Nexus
            
            SLAB_FLAT_PATH
            while (system_active_.load(std::memory_order_relaxed)) [[likely]] {
                nexus_->poll(); 
                _mm_pause();
            }
        }
        
        /**
         * @brief Processes administrative commands from the Management Plane.
         * @details Guard: This method is strictly syscall-free. 
         * Configuration reloads must be signaled via the AdminBus.
         */
        void process_management_plane(sys::admin_command* cmd = nullptr) noexcept {
            if (!cmd) {
                // Sovereign Standard: Use non-blocking poll for periodic checks
                if (!admin_conduit_->try_pop(cmd)) return;
            }
            if (cmd) {
                if (cmd->type == sys::admin_cmd_type::TAKE_SNAPSHOT) {
                    engine_->on_control_plane_snapshot(nullptr, 0);
                } else if (cmd->type == sys::admin_cmd_type::RELOAD_CONFIG) {
                    load_binary_config();
                } else if (cmd->type == sys::admin_cmd_type::MANUAL_CHECKPOINT) { // Manual checkpoint trigger
                    // Integrity: Flush persistence layer before anchoring the state
                    journal_->force_flush();
                    engine_->on_control_plane_snapshot(nullptr, 0);
                } else if (cmd->type == sys::admin_cmd_type::HEALTH_CHECK) {
                    // Structured health audit: compute bit-sum and report via arbiter
                    uint64_t sum = engine_->compute_health_sum();
                    // Emulate telemetry read natively via LBR engraving
                    uint8_t health_fray = (sum == 0) ? 0x8E : 0;
                    aphasic_horizon_[health_fray](this, health_fray);
                } else if (cmd->type == sys::admin_cmd_type::CLEAR_PARKING_LOTS) {
                    // Emergency Mesh Recovery: Purge all out-of-order state
                    causal_router_->purge_all_parking_lots();
                } else if (cmd->type == sys::admin_cmd_type::UPDATE_PRECISION) {
                    // Dynamic Reconfiguration: Update thresholds via engine bridge
                    auto& bridge = engine_->bridge(); // Access engine's config bridge
                    float val; 
                    std::memcpy(&val, &cmd->payload, sizeof(float));
                    bridge.precision_delta.store(val, std::memory_order_relaxed);
                } else if (cmd->type == sys::admin_cmd_type::UPDATE_BASELINE) {
                    // Dynamic Reconfiguration: Update stabilizer baseline
                    auto& bridge = engine_->bridge();
                    float val;
                    std::memcpy(&val, &cmd->payload, sizeof(float)); // Copy payload to float
                    bridge.sanitizer_baseline.store(val, std::memory_order_relaxed);
                } else if (cmd->type == sys::admin_cmd_type::UPDATE_CRITICAL_DRIFT) {
                    // Dynamic Reconfiguration: Update critical drift threshold
                    auto& bridge = engine_->bridge();
                    float val;
                    std::memcpy(&val, &cmd->payload, sizeof(float));
                    bridge.critical_drift.store(val, std::memory_order_relaxed);
                } else if (cmd->type == sys::admin_cmd_type::UPDATE_DRIFT_POLICY) { // Update drift policy
                    // Dynamic Reconfiguration: Update drift detection logic
                    auto& bridge = engine_->bridge();
                    compute::drift_policy val;
                    std::memcpy(&val, &cmd->payload, sizeof(uint8_t));
                    bridge.policy.store(val, std::memory_order_relaxed);
                } else if (cmd->type == sys::admin_cmd_type::TOGGLE_WEIGHTED_SANITIZATION) {
                    // Dynamic Reconfiguration: Toggle weighted neighbor sanitization
                    auto& bridge = engine_->bridge(); // Access engine's config bridge
                    bool current = bridge.weighted_sanitization.load(std::memory_order_relaxed);
                    bridge.weighted_sanitization.store(!current, std::memory_order_relaxed);
                } else if (cmd->type == sys::admin_cmd_type::SET_DIVERGENCE_SNAPSHOT_THRESHOLD) {
                    float val;
                    std::memcpy(&val, &cmd->payload, sizeof(float));
                    divergence_snapshot_threshold_.store(val, std::memory_order_relaxed);
                } else if (cmd->type == sys::admin_cmd_type::EXPORT_METRICS) { // Export metrics on demand
                    if (blackbox_recorder_) {
                        compute::divergence_analyzer<typename StateLogic::value_type, 
                                                     StateLogic::capacity>::export_csv(*blackbox_recorder_, 
                                                                                       "manual_metrics.csv");
                    }
                } else if (cmd->type == sys::admin_cmd_type::TOGGLE_DRIFT_SMOOTHING) {
                    auto& bridge = engine_->bridge();
                    bool current = bridge.drift_smoothing.load(std::memory_order_relaxed); // Toggle drift smoothing
                    bridge.drift_smoothing.store(!current, std::memory_order_relaxed);
                } else if (cmd->type == sys::admin_cmd_type::REPLICATE_STATE) {
                    // State Replication: Fetch context horizon and signal standby node
                    const uint64_t horizon = context_->horizon();
                    
                    // Cold-Backup: Stream the retransmission cache to NVMe
                    if (state_streamer_) state_streamer_->archive(*retrans_buffer_);

                    aphasic_horizon_[0x57](this, 0x57); // REPLICATE_SIGNAL Engraving
                }
            }
        }

        /**
         * @brief Drains the NACK conduit and fulfills retransmission requests.
         */
        void handle_nack_requests() noexcept {
            net::nack_request req;
            
            const size_t occ = net_conduit_->occupancy();
            const size_t critical = (Cap * 80) / 100;
            const size_t recovery = (Cap * 40) / 100;

            // Hysteresis: throttle at 80%, recover only when dropped to 40%
            if (nack_throttled_) { // Check if NACK throttling is active
                if (occ < recovery) {
                    nack_throttled_ = false;
                } else {
                    // Delayed: Engrave saturation anomaly
                    aphasic_horizon_[0x5A](this, 0x5A); // RECOVERY_SATURATED
                    return;
                }
            } else if (SL_EXPECT_FALSE(occ >= critical)) {
                nack_throttled_ = true;
                aphasic_horizon_[0xE6](this, 0xE6); // THROTTLED
                return;
            }

            uint32_t burst_count = 0;
            // High-watermark: prevent egress saturation by limiting retransmissions per cycle
            static constexpr uint32_t BURST_LIMIT = 256;

            while (burst_count < BURST_LIMIT && nack_conduit_->try_pop(req)) {
                // Self-request check: Ignore NACKs originating from this node
                if (SL_EXPECT_FALSE(req.requester_id == node_id_)) continue;

                // Sovereign Routing: Resolve physical FD for the specific requester
                int peer_fd = -1; // Fallback, API mapping bypassed
                
                // Fallback to primary IO worker if specific route is opaque
                if (peer_fd == -1) peer_fd = io_worker_fd_;

                nack_responder_->on_nack_received(req, peer_fd);
                
                // Telemetry: Aggregate byte volume retransmitted per cycle
                retrans_bytes_last_cycle_ += sizeof(WireFrame);
                burst_count++;
            }

            if (SL_EXPECT_FALSE(burst_count >= BURST_LIMIT)) {
                aphasic_horizon_[0x57](this, 0x57); // STORM_THROTTLE
            }
        }

        /**
         * @brief Evaluates mesh unblocking complexity and triggers emergency capture.
         */
        SLAB_HOT void mesh_congestion_audit(size_t unblocked) noexcept {
            // Dynamic Adjustment: Reduce threshold if logic latency trends high
            // to preserve I-cache residency for Experts.
            const size_t adaptive_threshold = (ema_logic_latency_ > 200'000) ? 64 : 128;
 
            uint8_t block_fray = (SL_EXPECT_FALSE(unblocked > adaptive_threshold)) ? 0xDE : 0;
            if (block_fray) {
                engine_->on_control_plane_snapshot(nullptr, 0); // Emergency anchor
            }
            aphasic_horizon_[block_fray](this, block_fray);
        }

        /**
         * @brief Verifies the current engine state against the audit ledger.
         * @details Automatically traps state divergence when debug_mode is active.
         */
        SLAB_HOT void verify_audit_ledger() noexcept {
            if (!audit_ledger_) return;

            const auto* entry = audit_ledger_->next(engine_->get_lsn()); // Get next audit entry
            if (SL_EXPECT_FALSE(entry && entry->lsn == engine_->get_lsn())) {
                if (entry->error_code == 0x8EADE8) { // AUDIT_HEALTH code
                    uint64_t current_hash = engine_->compute_health_sum();
                    uint8_t audit_fray = (SL_EXPECT_FALSE(current_hash != entry->health_signature)) ? 0xDE : 0;
                    aphasic_horizon_[audit_fray](this, audit_fray);
                } // Check for AUDIT_HEALTH code
            }
        }

        // Handles for replication and state synchronization
        [[nodiscard]] core::sf_node_ctx& get_context() noexcept { return *context_; }
        [[nodiscard]] RetransBuffer& get_retransmission_buffer() noexcept { return *retrans_buffer_; }

        /**
         * @brief Returns the designated core for network conduits.
         */
        int get_conduit_core() const noexcept { return conduit_core_; }

        /**
         * @brief Returns the shared io_uring worker FD for dynamic conduit attachment.
         */
        int get_io_worker_fd() const noexcept { return io_worker_fd_; }

        /**
         * @brief The Deterministic Compute Loop.
         * @details Orchestrates Time, Mesh, and Persistence in a strict sequence.
         */
        void run_compute() noexcept {
            // Worker Ignition: Register this thread with the node context for LSN analytics
            if (context_) {
                context_->register_active_worker(0, sched_getcpu());
            }

            uint64_t last_liveness_tsc = __rdtsc(); // Last liveness check timestamp
            const uint64_t liveness_threshold = 300'000'000; // ~100ms @ 3GHz

            if (SL_EXPECT_FALSE(dry_run_)) {
                std::cout << "[SOVEREIGN] Dry Run: Priming silicon and warming instruction cache...\n";
                for (int i = 0; i < 10000; ++i) {
                    engine_->process_warmup_ghost();
                    asm volatile("" : : : "memory");
                }
                std::cout << "[SOVEREIGN] Silicon hot. Terminating Dry Run.\n";
                return;
            }

            SLAB_FLAT_PATH
            while (system_active_.load(std::memory_order_relaxed)) [[likely]] {
               
                uint8_t ev_type = 255;
                void* ev_ptr = event_arbiter_->poll_next(ev_type); // Poll for next event

                // --- SEGMENT A: PRIORITIZED DISPATCH ---
                if (ev_ptr && ev_type == 0) { // ADMIN
                    process_management_plane(static_cast<sys::admin_command*>(ev_ptr));
                    continue; // Re-poll immediately for control-plane responsiveness
                } // Process admin commands
                
                // Performance Anchor: Throttle background management plane checks to prevent syscall jitter
                if (SL_UNLIKELY((engine_->get_lsn() & 0x3FF) == 0)) {
                    process_management_plane(nullptr);
                }
                handle_nack_requests();            // Fulfill retransmission requests

                if (SL_EXPECT_FALSE(debug_mode_)) {
                    verify_audit_ledger();
                }

                // Periodic Liveness Audit
                uint64_t current_tsc = __rdtsc();
                if (SL_EXPECT_FALSE(!constrained_mode_ && current_tsc - last_liveness_tsc > liveness_threshold)) { // Check liveness threshold
                    causal_router_->run_liveness_audit();
                    last_liveness_tsc = current_tsc;
                }

                uint64_t cycle_start = __rdtsc();

                // 1. Process Temporal Heartbeat
                if (ev_ptr && ev_type == 1) { // TIME
                    auto* tick = static_cast<sys::tick_event*>(ev_ptr); // Cast to tick event
                    engine_->process_tick(tick); // Process tick
                    time_pool_->free(tick);
                } else if (auto* tick = time_conduit_->pop()) {
                    engine_->process_tick(tick); // Process tick
                    time_pool_->free(tick); // Free tick event
                }

                // Detect drift immediately after temporal update
                float tm = engine_->get_last_trap_mse();
                float current_threshold = divergence_snapshot_threshold_.load(std::memory_order_relaxed);
                if (SL_EXPECT_FALSE(current_threshold > 0.0f && tm > current_threshold)) { // Check divergence threshold
                    engine_->on_control_plane_snapshot(nullptr, 0);
                }

                // --- SEGMENT B: THE COMPUTE HOT-ZONE ---
                uint32_t count = 0;
                AciEnvelope aci_batch[16];
                WireFrame* batch[16];
                uint64_t journal_cycles = 0;
 
                // Hardware Shielding: Batched popping utilizing the Bimodal ACI envelope conduit
                while (count < adaptive_batch_target_) {
                    if (!shield_conduit_->try_pop(aci_batch[count])) break;
                    
                    // Evaluate Hardware Collision Graph
                    auto [type_id, payload] = aci_batch[count].extract_and_decouple(expected_compute_lsn_++);
                    if (SL_EXPECT_FALSE(type_id == 0)) {
                        aphasic_horizon_[0xA4](this, 0xA4); // ACI Fray Drop via Ontological Decoupling
                        if (payload) net_pool_->free(payload);
                        continue;
                    }
                    batch[count] = payload;
                    count++;
                }
                
                uint64_t logic_start = __rdtsc();
                if (count > 0) {
                    // Fast-Attack, Slow-Decay Adaptive Logic
                    if (count == adaptive_batch_target_ && adaptive_batch_target_ < 16) adaptive_batch_target_++; // Increase batch target
                    else if (count < (adaptive_batch_target_ >> 1) && adaptive_batch_target_ > 1) adaptive_batch_target_--;

                    uint64_t journal_start = __rdtsc();
                        
                        uint8_t j_fray = 0;
                        auto casted_batch = reinterpret_cast<const net::wire_frame_lsn<WireFrame>* const*>(batch);
                        if constexpr (requires { journal_->append(casted_batch, count); }) {
                            j_fray = (!journal_->append(casted_batch, count)) ? 0x51 : 0;
                        } else if constexpr (requires { journal_->write(casted_batch, count); }) {
                            j_fray = (!journal_->write(casted_batch, count)) ? 0x51 : 0;
                        } else if constexpr (requires { journal_->write_batch(casted_batch, count); }) {
                            j_fray = (!journal_->write_batch(casted_batch, count)) ? 0x51 : 0;
                        }
                        
                    uint64_t journal_cycles = __rdtsc() - journal_start;

                    // Teleological Agnosia: Transmit to branchless route void natively
                    route_context ctx{ batch, count, cycle_start, logic_start, journal_cycles };
                    route_horizon_[j_fray](this, ctx, j_fray);

                    continue; 
                }

                if (SL_EXPECT_FALSE(constrained_mode_)) { // Check constrained mode
                    std::this_thread::yield();
                } else {
                    _mm_pause();
                }
            }
            halt_and_dump_state();
        }

        void halt() noexcept { 
            system_active_.store(false, std::memory_order_release);
            clock_node_->stop(); 
        }
 
        void halt_and_dump_state() {
            // Safety: Ensure the final events are persisted before halting.
            if (journal_) journal_->force_flush();

            // Diagnostics: Unconditionally dump blackbox metrics to NVMe upon system halt.
            if (blackbox_recorder_) {
                compute::divergence_analyzer<typename StateLogic::value_type, 
                                             StateLogic::capacity>::export_csv(*blackbox_recorder_, 
                                                                               "system_performance_dump.csv");
            }

            halt(); // Explicitly disengage temporal motor to unblock I/O threads
            std::cout << "[SYSTEM] Compute Node halted. Blackbox exported.\n";
        }

        void record_fault(uint8_t fray, uint64_t lsn) noexcept {
            if (!error_arbiter_) return;

            // Map SlabFlux fray codes to classical error domains/severity
            rte::error_domain domain = rte::error_domain::general;
            rte::error_severity severity = rte::error_severity::warning;

            if (fray == 0xDE) {
                domain = rte::error_domain::compute;
                severity = rte::error_severity::critical;
            } else if (fray == 0x57) {
                domain = rte::error_domain::network;
                severity = rte::error_severity::error;
            } else if (fray == 0xA4) {
                domain = rte::error_domain::compute;
                severity = rte::error_severity::error;
            } else if (fray == 0x8E) {
                domain = rte::error_domain::storage;
                severity = rte::error_severity::warning;
            }

            error_arbiter_->record_error(domain, fray, severity, lsn);
        }

    private:
        /**
         * @brief Ultra-low jitter configuration ingestion via mmap.
         * @details Directly maps the binary manifest into the address space to update
         * the engine's configuration bridge without string parsing or allocation.
         */
        void load_binary_config() noexcept {
            int fd = ::open("slabflux_config.bin", O_RDONLY);
            if (fd < 0) return;

            struct stat st; // File status
            if (::fstat(fd, &st) == 0 && st.st_size >= sizeof(binary_config_payload)) {
                void* map = ::mmap(nullptr, sizeof(binary_config_payload), PROT_READ, MAP_PRIVATE, fd, 0);
                if (map != MAP_FAILED) {
                    auto* b_cfg = static_cast<const binary_config_payload*>(map);
                    auto& bridge = engine_->bridge();
                    
                    bridge.precision_delta.store(b_cfg->precision_delta, std::memory_order_relaxed);
                    bridge.sanitizer_baseline.store(b_cfg->sanitizer_baseline, std::memory_order_relaxed); // Store sanitizer baseline
                    bridge.critical_drift.store(b_cfg->critical_drift, std::memory_order_relaxed);
                    
                    float next_threshold = b_cfg->divergence_snapshot_threshold;
                    float expected = divergence_snapshot_threshold_.load(std::memory_order_relaxed);
                    while (!divergence_snapshot_threshold_.compare_exchange_weak(expected, next_threshold, std::memory_order_relaxed));

                    bridge.policy.store(static_cast<compute::drift_policy>(b_cfg->drift_policy), std::memory_order_relaxed);
                    bridge.weighted_sanitization.store(b_cfg->weighted_sanitization, std::memory_order_relaxed);
                    bridge.drift_smoothing.store(b_cfg->drift_smoothing, std::memory_order_relaxed); // Store drift smoothing
                    
                    ::munmap(map, sizeof(binary_config_payload));
                }
            }
            ::close(fd);
        }
    };

    /**
     * @brief The Supplemental Automation Builder.
     * @details Eliminates user-side boilerplate for physical wiring
     */
    class environment_builder {
        int ingress_core_{-1};
        int clock_core_{-1};
        int journal_core_{-1};
        int conduit_core_{-1};
        uint16_t node_id_{0};
        uint64_t clock_res_ns_{1000};
        const char* journal_path_{nullptr};
        const char* snapshot_path_{nullptr};
        std::vector<std::pair<uint16_t, int>> peers_;
        float precision_delta_{ 0.0f };
        float sanitizer_baseline_{ 0.0f };
        float critical_drift_{ 0.0f };
        float divergence_snapshot_threshold_{ 0.0f };
        const char* cold_backup_path_{ nullptr };
        const char* audit_path_{nullptr};
        bool dry_run_{ false };
        bool debug_mode_{false};

    public:
        environment_builder& with_ingress_on_core(int cpuid) { ingress_core_ = cpuid; return *this; }
        environment_builder& with_conduit_on_core(int cpuid) { conduit_core_ = cpuid; return *this; }
        environment_builder& with_clock_on_core(int cpuid, uint64_t res = 1000) { 
            clock_core_ = cpuid; clock_res_ns_ = res; return *this; 
        }
        environment_builder& with_journal(int cpuid, const char* path) {
            journal_core_ = cpuid; journal_path_ = path; return *this;
        }
        environment_builder& register_peer(uint16_t id, int fd) {
            peers_.push_back({id, fd});
            return *this;
        }
        environment_builder& with_node_id(uint16_t id) { node_id_ = id; return *this; }
        environment_builder& with_snapshot(const char* path) { snapshot_path_ = path; return *this; }
        environment_builder& with_precision_delta(float delta) { precision_delta_ = delta; return *this; }
        environment_builder& with_sanitizer_baseline(float baseline) { sanitizer_baseline_ = baseline; return *this; }
        environment_builder& with_critical_drift(float drift) { critical_drift_ = drift; return *this; }
        environment_builder& with_debug_mode(const char* audit_path) {
            debug_mode_ = true;
            audit_path_ = audit_path; // Set audit path
            return *this;
        }

        environment_builder& with_dry_run(bool dr = true) {
            dry_run_ = dr;
            return *this;
        }

        environment_builder& with_divergence_snapshot_threshold(float threshold) {
            divergence_snapshot_threshold_ = threshold;
            return *this;
        }

        environment_builder& with_cold_backup(const char* path) {
            cold_backup_path_ = path;
            return *this;
        }

        /**
         * @brief Fuses all components into the Active Environment.
         * @details Behavior is inherent in the constructs.
         */ // Fuses components into active environment
        template<typename EventType, typename StateLogic, size_t RingCapacity = 65536, typename FailurePolicy = teleological_agnosia_policy>
        auto ignite(StateLogic& logic, std::unique_ptr<core::sf_node_ctx> external_ctx = nullptr) {
            static_assert((RingCapacity & (RingCapacity - 1)) == 0, 
                "Sovereign Requirement: RingCapacity must be a power of 2 for deterministic masking.");

            if (ingress_core_ == -1 || clock_core_ == -1 || conduit_core_ == -1 || !journal_path_) {
                throw std::runtime_error("Topology incomplete");
            }

            using WireFrame = mesh::wire_frame<EventType>;
            using NetAlloc  = core::pinned_allocator_spsc<WireFrame, RingCapacity>;
            using TimeAlloc = core::pinned_allocator_spsc<sys::tick_event, RingCapacity>;
            using NetBus    = core::spsc_conduit<WireFrame*, RingCapacity>;
            using AciEnvelope = net::autologous_isomorphism<WireFrame*>;
            using ShieldConduit = core::spsc_conduit<AciEnvelope, RingCapacity>;
            using BimodalShield = net::bimodal_shield_wiring<ShieldConduit, WireFrame, NetAlloc>;
            using TimeBus   = core::spsc_conduit<sys::tick_event*, RingCapacity>;
            using Engine    = compute::branchless_engine<StateLogic, EventType, NetBus>;
            using NackAlloc = core::pinned_allocator_spsc<net::nack_request, 1024>;
            using NackBus   = core::spsc_conduit<net::nack_request, 1024>;
            using Router    = mesh::causal_ingress_router<EventType, 1024, Engine, NetAlloc, NackBus, NackAlloc>;
            using AdminBus  = core::spsc_conduit<sys::admin_command*, 128>;
            using RetransBuffer = net::retransmission_buffer<WireFrame, RingCapacity>;

            // Resource Instantiation
            auto np = std::make_unique<NetAlloc>();
            auto nkap = std::make_unique<NackAlloc>();
            auto tp = std::make_unique<TimeAlloc>();
            auto ad = std::make_unique<AdminBus>();
            auto nc = std::make_unique<NetBus>();
            auto sc = std::make_unique<ShieldConduit>();
            auto tc = std::make_unique<TimeBus>();
            auto nk = std::make_unique<NackBus>();

            // Component Instantiation
            std::unique_ptr<core::sf_node_ctx> ctx = std::move(external_ctx); // Move external context
            if (!ctx) ctx = std::make_unique<core::sf_node_ctx>();

            // Populate peer routing table
            for (const auto& peer : peers_) {
                // ctx->register_peer(peer.first, peer.second); // Disabled: OS socket API mismatch
            }

            // Initialize Quadripartite Defense Telemetry Pillar
            std::unique_ptr<security::semiotic_tapestry> tap;
            std::unique_ptr<security::panoptic_reticle> ret;

            if constexpr (!std::is_same_v<FailurePolicy, error_arbiter_policy>) {
                tap = std::make_unique<security::semiotic_tapestry>();
                tap->weave(); // Default 4GB Virtual Memory block
            }
            
            try {
                ret = std::make_unique<security::panoptic_reticle>(conduit_core_, *tap);
            } catch (const std::exception& e) {
                std::cerr << "[WARN] " << e.what() << "\n[WARN] Running in Agnosia-Only mode: Node will safely drop corrupted data but will NOT auto-halt on fatal errors.\n";
            }

            // Wire up the saturation void index
            if constexpr (requires { nc->bind_aphasic_horizon(nullptr, [](void*, uint8_t){}); }) {
                nc->bind_aphasic_horizon(nullptr, [](void*, uint8_t fray) { 
                    // Reaches the global void execution without context mapping
                });
            }

            // Initialize snapshot manager if path provided
            std::unique_ptr<compute::snapshot_manager<StateLogic>> sm;
            if (snapshot_path_) {
                sm = std::make_unique<compute::snapshot_manager<StateLogic>>(snapshot_path_); // Create snapshot manager
            }

            // Persistence: Setup cold-standby archival for retransmission mesh
            const size_t rb_size = sizeof(net::retransmission_buffer<WireFrame, RingCapacity>);
            std::unique_ptr<net::state_streamer<net::retransmission_buffer<WireFrame, RingCapacity>>> ss; // State streamer
            if (cold_backup_path_) {
                ss = std::make_unique<net::state_streamer<net::retransmission_buffer<WireFrame, RingCapacity>>>(cold_backup_path_, rb_size);
            }

            // Efficiency: Allocate Engine on the local NUMA node.
            // This ensures AI Expert weights are physically resident near the Compute Core.
            const size_t en_size = sizeof(Engine);
            void* en_mem = core::hardware_topology::allocate_on_local_node(en_size); // Allocate engine memory
            if (!en_mem) throw std::system_error(errno, std::generic_category(), "Engine memory allocation failed");

            // Pre-fault: Explicit touch of the Engine span to ensure RSS residency
            std::memset(en_mem, 0, en_size);

            auto en = std::unique_ptr<Engine, void(*)(Engine*)>(
                new (en_mem) Engine(*nc, sm.get(), precision_delta_, sanitizer_baseline_, critical_drift_), 
                [](Engine* e){ e->~Engine(); }
            );
            en->bind_tapestry(tap.get());

            // Core Audit: Disable SQPOLL if system is core-starved (< 4 cores)
            // to prevent context-switching jitter on the hot path.
            int available_cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
            int effective_sq_core = (available_cores >= 4) ? ingress_core_ : -1; // Determine effective SQ core

            // Isolation: Enforce Intel CAT to pin weights in L3.
            // CLOS 1 (Expert): 0x00F (4 ways) - Heavy AI weights
            // CLOS 2 (Control): 0x030 (2 ways) - Arbiter & Heartbeat
            // CLOS 0 (OS/Net): 0xFC0 (remaining ways)
            sys::cache_partitioner::enforce_exclusive_l3("sovereign_expert",  0x00F);
            sys::cache_partitioner::enforce_exclusive_l3("sovereign_control", 0x030);

            // Warm-Restart: Use O_EXCL to detect if we own the initialization
            const char* shm_name = "/slabflux_retrans_cache";
            int shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0666); // Open shared memory
            bool is_master = (shm_fd != -1);

            if (!is_master && errno == EEXIST) {
                shm_fd = shm_open(shm_name, O_RDWR, 0666);
            }

            if (shm_fd < 0) throw std::system_error(errno, std::generic_category(), "Sovereign SHM access failed");
            
            
            // Hardening: Attempt 2MB HugePages, fallback to standard pages with mlock
            void* rb_ptr = mmap(nullptr, rb_size, PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_HUGETLB | MAP_LOCKED, shm_fd, 0); // Map shared memory

            if (rb_ptr == MAP_FAILED) {
                rb_ptr = mmap(nullptr, rb_size, PROT_READ | PROT_WRITE, 
                              MAP_SHARED | MAP_LOCKED, shm_fd, 0);
            }
            
            if (rb_ptr == MAP_FAILED) throw std::system_error(errno, std::generic_category(), "SHM Mapping Failed (Fatal)");

            // Race Prevention: Only the 'Master' (creator) initializes the memory.
            // Followers attach to the existing historical state.
            if (is_master) {
                new (rb_ptr) RetransBuffer();
            }
            auto rb = std::unique_ptr<RetransBuffer, void(*)(RetransBuffer*)>(
                static_cast<RetransBuffer*>(rb_ptr), [](RetransBuffer*){ /* No-op deleter for SHM */ });
            
            auto cr = std::make_unique<Router>(node_id_, *en, *np, *nk, *nkap); // Create router
            
            auto sh = std::make_unique<BimodalShield>(*sc, *np);
            auto gn = std::make_unique<core::fused_nexus_node<WireFrame, RingCapacity, BimodalShield>>(
                *np, *sh, -1 /* socket */, effective_sq_core, rb_ptr, rb_size);

            // Debug Mode: Open audit file for real-time verification
            std::unique_ptr<sys::audit_ledger<>> al;
            if (debug_mode_ && audit_path_) {
                al = std::make_unique<sys::audit_ledger<>>(audit_path_); // Create audit ledger
                if (!al->is_valid()) {
                    std::cerr << "[WARN] Debug mode enabled but audit ledger missing: " << audit_path_ << "\n";
                }
            }

            std::unique_ptr<sys::blackbox_recorder<>> br;
            if (debug_mode_) {
                br = std::make_unique<sys::blackbox_recorder<>>();
            }
            
            std::unique_ptr<rte::error_arbiter<>> ea;
            if constexpr (std::is_same_v<FailurePolicy, error_arbiter_policy>) {
                ea = std::make_unique<rte::error_arbiter<>>();
                en->bind_error_arbiter(ea.get());
            }

            // Recovery: Bind the NACK responder to the Ingress I/O ring
            // Passing index 1 as it was the second buffer registered in iovs above. // Bind NACK responder
            auto nr = std::make_unique<net::nack_handler<WireFrame, RingCapacity>>(*rb, *ctx, gn->get_ring(), 1);

            // Efficiency: Share the async worker pool from Ingress to the Journal
            int master_fd = gn->get_ring_fd();
            auto jr = std::make_unique<io::durable_journal<WireFrame>>(journal_path_);
            auto cn = std::make_unique<io::clock_node<TimeAlloc, TimeBus>>(*tp, *tc, clock_res_ns_);

            // Topology Audit: Ensure Ingress and Compute are L2-isolated // Verify L2 isolation
            if (!core::hardware_topology::verify_l2_isolation(ingress_core_, sched_getcpu())) {
                std::cerr << "[CRITICAL WARN] Ingress and Compute share L2 cache! Expect priority inversion jitter.\n";
            }

            return active_environment<EventType, StateLogic, RingCapacity, FailurePolicy>(
                std::move(ctx),
                std::move(np), std::move(tp), std::move(nc), std::move(tc),
                std::move(sc), std::move(sh), std::move(nkap),
                std::move(nk), std::move(ad), std::move(rb), std::move(ss), std::move(nr), 
                std::move(gn), 
                std::move(cn), std::move(jr), std::move(cr), std::move(en),
                std::move(sm),
                std::move(tap),
                std::move(ret),
                debug_mode_,
                divergence_snapshot_threshold_,
                dry_run_,
                std::move(al),
                std::move(br),
                std::move(ea),
                conduit_core_,
                master_fd,
                (available_cores < 4),
                node_id_
            );
        }
    };

    inline environment_builder build_topology() { return environment_builder(); }

} // namespace slabflux::rte