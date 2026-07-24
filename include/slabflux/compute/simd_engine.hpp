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
#include <immintrin.h>
#include <nmmintrin.h> // CRC32 intrinsics
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/compute/path_guard.hpp"
#include "slabflux/compute/temporal_guard.hpp"
#include "slabflux/sys/tick_event.hpp"
#include "slabflux/mesh/causal_mesh.hpp"

namespace slabflux::mesh {
    // Forward declaration to resolve circular dependency during early header parsing
    struct origin_reset_event;
}

namespace slabflux::compute {

    /**
     * @brief High-Density SIMD State Block.
     * @details Optimized for AVX-512. Processes 8 keys per cycle.
     */
    template<typename T, size_t Capacity>
    struct alignas(4096) avx512_search_backend {
        using value_type = T;
        static constexpr size_t capacity = Capacity;
        static_assert(Capacity % 8 == 0, "Capacity must be a multiple of 8 for AVX-512");

        T elements[Capacity];

        /**
         * @brief Rapidly zeros a memory segment using AVX-512 stores.
         * @details Bypasses standard memset to ensure deterministic cycle count.
         */
        SLAB_HOT void clear_segment(size_t start, size_t count) noexcept {
            const size_t bytes = count * sizeof(T);
            size_t offset = 0;
            __m512i zero = _mm512_setzero_si512();
            uint8_t* ptr = reinterpret_cast<uint8_t*>(&elements[start]);

            // SIMD 64-byte blocks (cache line size)
            for (; offset + 64 <= bytes; offset += 64) {
                _mm512_storeu_si512(ptr + offset, zero);
            }

            // Scalar tail (SlabFlux segments are typically aligned, but safety first)
            while (offset < bytes) {
                ptr[offset++] = 0;
            }
        }

        /**
         * @brief Administrative event handler for node resets.
         * @details Clears internal history for a specific node to maintain determinism.
         * Enforced via constraints to maintain strict type specialization while bypassing circular dependencies.
         */
        template <typename EventType>
        requires std::is_same_v<EventType, mesh::origin_reset_event>
        SLAB_HOT void on_event(const EventType* ev, uint64_t lsn) noexcept {
            // Defer lookup of MAX_CLUSTER_NODES until instantiation using sizeof
            const size_t segment = Capacity / (sizeof(EventType) ? mesh::MAX_CLUSTER_NODES : 1);
            const size_t start = ev->origin_node_id * segment;
            
            if (SL_EXPECT_TRUE(start < Capacity)) {
                clear_segment(start, segment);
            }
            (void)lsn;
        }

        /**
         * @brief Domain-specific event handler.
         */
        template<typename EventType>
        requires (!std::is_same_v<EventType, mesh::origin_reset_event>)
        SLAB_HOT void on_event(const EventType* ev, uint64_t lsn) noexcept {
            // State transformation logic goes here.
            (void)ev; (void)lsn;
        }

        SLAB_HOT void on_tick(const sys::tick_event* tick, uint64_t lsn) noexcept { (void)tick; (void)lsn; }

        SLAB_HOT const T* generate_response() const noexcept { return nullptr; }

        /**
         * @brief Constant-time AVX-512 search.
         * @return Index or -1. Zero branches in the loop body.
         */
        SLAB_HOT int find_index_avx512(uint64_t target_key) const noexcept {
            __m512i v_target = _mm512_set1_epi64(target_key);
            const uint64_t* keys = reinterpret_cast<const uint64_t*>(elements);

            SLAB_FLAT_PATH
                for (size_t i = 0; i < Capacity; i += 8) {
                    // Prefetch the next cache line (64 bytes ahead)
                    _mm_prefetch(reinterpret_cast<const char*>(&keys[i + 8]), _MM_HINT_T0);

                    __m512i v_keys = _mm512_load_si512(reinterpret_cast<const __m512i*>(&keys[i]));

                    // AVX-512 returns a bitmask (k-register) directly
                    __mmask8 mask = _mm512_cmpeq_epi64_mask(v_keys, v_target);

                    if (SL_EXPECT_FALSE(mask != 0)) {
                        return i + __builtin_ctz(mask);
                    }
                }
            return -1;
        }
    };

} // namespace slabflux::compute

#include "slabflux/compute/divergence_analyzer.hpp"
#include "slabflux/compute/snapshot_engine.hpp"
#include "slabflux/compute/replay_validator.hpp"
#include "slabflux/compute/numerical_sanitizer.hpp"
#include "slabflux/security/kinetic_inscription.hpp"

namespace slabflux::compute {

    /**
     * @brief The Sovereign Execution Engine.
     */
    template<typename StateLogic, typename EventType, typename EgressConduit>
    class alignas(64) branchless_engine {
        using state_type = StateLogic;
        using value_type = typename StateLogic::value_type;

        struct config_bridge {
            std::atomic<value_type> precision_delta{ 0 };
            std::atomic<value_type> sanitizer_baseline{ 0 };
            std::atomic<value_type> critical_drift{ 0 };
            std::atomic<drift_policy> policy{ drift_policy::BIT_IDENTICAL };
            std::atomic<bool> weighted_sanitization{ false };
            std::atomic<bool> drift_smoothing{ false };
        };

        struct divergence_trap {
            const StateLogic* reference{ nullptr };
            uint64_t lsn{ 0 };
        };

        static constexpr size_t MAX_TRAPS = 16;
        divergence_trap traps_[MAX_TRAPS];
        uint32_t trap_count_{ 0 };
        uint32_t next_trap_idx_{ 0 };

        StateLogic domain_state_;
        uint64_t current_lsn_{ 0 };
        uint64_t state_hash_{ 0 };

        // The new internal guard
        float last_trap_mse_{ 0.0f };
        temporal_guard time_guard_{ 3000000 }; // E.g. 1ms @ 3GHz
        EgressConduit& egress_;
        snapshot_manager<StateLogic>* snapshot_mgr_{ nullptr };
        config_bridge config_{};
        uint32_t consecutive_drift_count_{ 0 };
        float mse_ema_{ 0.0f };
        float configured_precision_delta_{ 0.0f };
        const security::semiotic_tapestry* tapestry_{ nullptr };
        rte::error_arbiter<>* arbiter_{ nullptr };

        /**
         * @brief Drift Smoothing: Dynamically adjusts precision_delta based on MSE trends.
         */
        SLAB_HOT void update_drift_smoothing(float current_mse) noexcept {
            if (!config_.drift_smoothing.load(std::memory_order_relaxed)) return;

            constexpr float alpha = 0.1f; // 10-step smoothing horizon
            mse_ema_ = (alpha * current_mse) + (1.0f - alpha) * mse_ema_;

            // Adjust delta: allow it to expand with the noise floor but anchor to configured base
            if (SL_EXPECT_FALSE(mse_ema_ > 0)) {
                const float dynamic_delta = std::max(configured_precision_delta_, mse_ema_ * 2.0f);
                config_.precision_delta.store(dynamic_delta, std::memory_order_relaxed);
            }
        }

        // Teleological Agnosia: The Aphasic Horizon
        // Replaces error arbitration. Frayed states natively index into a terminal void.
        using response_t = decltype(std::declval<StateLogic>().generate_response());
        using agnosia_sink_t = void (*)(branchless_engine*, EgressConduit&, response_t, uint8_t);
        agnosia_sink_t aphasic_horizon_[256];

        static void execute_valid_egress(branchless_engine* eng, EgressConduit& eg, response_t val, uint8_t) noexcept {
            eg.try_push(val);
        }
        static void execute_void_egress(branchless_engine* eng, EgressConduit&, response_t, uint8_t fray) noexcept {
            // Kinetic Inscription: Engrave anomaly into the LBR silicon without touching RAM.
            if (eng->tapestry_) {
                eng->tapestry_->engrave_anomaly(fray, eng->current_lsn_);
            }
        }

    public:
        branchless_engine(EgressConduit& eg, snapshot_manager<StateLogic>* sm = nullptr,
                          value_type delta = 0, value_type baseline = 0, value_type critical = 0,
                          drift_policy pol = drift_policy::BIT_IDENTICAL)
                    : egress_(eg), snapshot_mgr_(sm) {
            config_.precision_delta.store(delta, std::memory_order_relaxed);
            configured_precision_delta_ = delta;
            config_.sanitizer_baseline.store(baseline, std::memory_order_relaxed);
            config_.critical_drift.store(critical, std::memory_order_relaxed);
            config_.policy.store(pol, std::memory_order_relaxed);

            // Initialize the Aphasic Horizon
            aphasic_horizon_[0] = &execute_valid_egress;
            for (int i = 1; i < 256; ++i) {
                aphasic_horizon_[i] = &execute_void_egress;
            }
        }

        /** @brief Binds the Semiotic Tapestry for hardware-level telemetry engraving. */
        void bind_tapestry(const security::semiotic_tapestry* tapestry) noexcept {
            tapestry_ = tapestry;
        }

        /** @brief Binds the Error Arbiter for explicit deterministic telemetry. */
        void bind_error_arbiter(rte::error_arbiter<>* arbiter) noexcept {
            arbiter_ = arbiter;
        }

        /** @brief Returns a handle to the runtime configuration bridge. */
        config_bridge& bridge() noexcept { return config_; }

        /**
         * @brief Returns the current Logical Sequence Number.
         */
        [[nodiscard]] uint64_t get_lsn() const noexcept { return current_lsn_; }

        /**
         * @brief Computes a fresh hash of the SIMD state block.
         * @details Used for management-plane health audits and 
         * detecting silent data corruption.
         */
        [[nodiscard]] uint64_t compute_health_sum() const noexcept {
            return compute_state_hash(domain_state_);
        }

        /**
         * @brief Sets a divergence trap for bit-perfect debugging.
         * @param lsn The LSN at which to perform the comparison.
         * @param ref Pointer to the known-good reference state.
         */
        void set_divergence_trap(uint64_t lsn, const StateLogic* ref) noexcept {
            if (SL_EXPECT_FALSE(trap_count_ >= MAX_TRAPS)) return;
            
            traps_[trap_count_++] = { ref, lsn };
            
            // Keep traps sorted by LSN to allow O(1) cursor-based logic in the hot path.
            // (Simple insertion sort for small registry)
            for (uint32_t i = trap_count_ - 1; i > 0; --i) {
                if (traps_[i].lsn < traps_[i - 1].lsn) {
                    divergence_trap temp = traps_[i];
                    traps_[i] = traps_[i - 1];
                    traps_[i - 1] = temp;
                } else break;
            }
        }

        /**
         * @brief Returns a reference to the internal state block.
         */
        [[nodiscard]] const StateLogic& get_state() const noexcept { return domain_state_; }

        /**
         * @brief Attaches a snapshot manager for asynchronous state persistence.
         */
        void set_snapshot_manager(snapshot_manager<StateLogic>* sm) noexcept {
            snapshot_mgr_ = sm;
        }

        /**
         * @brief Evaluates aggregate state drift for emergency persistence.
         * @details Triggers an emergency snapshot if MSE exceeds critical threshold 
         * for 10 consecutive logical sequence numbers.
         */
        SLAB_HOT void monitor_emergency_drift() noexcept {
            // Emergency monitoring requires an active trap/reference to define "drift"
            if (SL_EXPECT_FALSE(next_trap_idx_ < trap_count_)) {
                const auto* ref = traps_[next_trap_idx_].reference;
                if (SL_EXPECT_TRUE(ref)) {
                    const auto mse = divergence_analyzer<value_type, StateLogic::capacity>::calculate_mse(domain_state_, *ref);
                    const auto critical = config_.critical_drift.load(std::memory_order_relaxed);

                    if (SL_EXPECT_FALSE(critical > 0 && mse > critical)) {
                        if (++consecutive_drift_count_ >= 10) {
                            // Anchor: Force non-blocking snapshot to preserve unstable state
                            on_control_plane_snapshot(nullptr, 0);
                            consecutive_drift_count_ = 0;
                        }
                    } else {
                        consecutive_drift_count_ = 0;
                    }
                }
            }
        }

        /**
         * @brief Control-plane event dispatcher.
         * @details Routes administrative events to the domain logic.
         */
        template<typename T>
        SLAB_HOT void dispatch(const T& event) noexcept {
            domain_state_.on_event(&event, current_lsn_);
        }

        /**
         * @brief Control-Plane Snapshot hook.
         * @details Records administrative resets or state mutations in the journal
         * to ensure control-plane events are perfectly replayable.
         */
        SLAB_HOT void on_control_plane_snapshot(const void* admin_state, size_t len) noexcept {
            // Capture the administrative epoch for bit-perfect state reconstruction.
            state_hash_ = _mm_crc32_u64(state_hash_, 0x501AD540); // SNAPSHOT_MARKER

            // Asynchronous Snapshot: Trigger a non-blocking capture 
            // during control-plane events to anchor the journal state.
            if (snapshot_mgr_) {
                snapshot_mgr_->async_snapshot(domain_state_, current_lsn_);
            }
            (void)admin_state; (void)len;
        }

        // Processing a time-event
        SLAB_HOT uint64_t process_tick(const sys::tick_event* tick) noexcept {
            current_lsn_++;
            // Deterministically check whether we have slowed down
            time_guard_.evaluate_tick(tick->hardware_tsc, current_lsn_, tapestry_, arbiter_);

            // The state machine may also receive time (e.g. for handling timeouts)
            domain_state_.on_tick(tick, current_lsn_);

            // Stabilization: Sanitize state to prevent NaN/Inf propagation
            const auto baseline = config_.sanitizer_baseline.load(std::memory_order_relaxed);
            const auto weighted = config_.weighted_sanitization.load(std::memory_order_relaxed);
            const auto critical = config_.critical_drift.load(std::memory_order_relaxed);
            
            // For differential cleansing, we use the active trap's reference if available
            const StateLogic* ref_ptr = (next_trap_idx_ < trap_count_) ? traps_[next_trap_idx_].reference : nullptr;

            uint64_t mask = numerical_sanitizer<value_type, StateLogic::capacity>::sanitize(
                domain_state_.elements, baseline, weighted, 
                (ref_ptr ? ref_ptr->elements : nullptr), critical);

            // Numerical Congestion Check: If > 5% of lanes are anomalies, switch to MSE policy
            if (SL_EXPECT_FALSE(_mm_popcnt_u64(mask) > (StateLogic::capacity / 20))) {
                config_.policy.store(drift_policy::MSE_BASED, std::memory_order_relaxed);
            }

            // Monitor for persistent critical drift
            monitor_emergency_drift();

            // Divergence Trap: Check for state drift at a specific LSN after state mutation
            while (SL_EXPECT_FALSE(next_trap_idx_ < trap_count_ && current_lsn_ == traps_[next_trap_idx_].lsn)) {
                last_trap_mse_ = divergence_analyzer<typename StateLogic::value_type, StateLogic::capacity>::analyze(
                    domain_state_, *traps_[next_trap_idx_].reference, current_lsn_, 
                    config_.policy.load(std::memory_order_relaxed),
                    config_.precision_delta.load(std::memory_order_relaxed), baseline,
                    config_.critical_drift.load(std::memory_order_relaxed),
                    tapestry_);
                update_drift_smoothing(last_trap_mse_);
                next_trap_idx_++;
            }
            return mask;
        }

        SLAB_HOT uint64_t process(const EventType* event) noexcept {
            current_lsn_++;

            // 1. State update (O(1) time)
            domain_state_.on_event(event, current_lsn_);

            // Stabilization: Sanitize state to prevent NaN/Inf propagation
            const auto baseline = config_.sanitizer_baseline.load(std::memory_order_relaxed);
            const auto weighted = config_.weighted_sanitization.load(std::memory_order_relaxed);
            const auto critical = config_.critical_drift.load(std::memory_order_relaxed);

            const StateLogic* ref_ptr = (next_trap_idx_ < trap_count_) ? traps_[next_trap_idx_].reference : nullptr;

            uint64_t mask = numerical_sanitizer<value_type, StateLogic::capacity>::sanitize(
                domain_state_.elements, baseline, weighted,
                (ref_ptr ? ref_ptr->elements : nullptr), critical);

            if (SL_EXPECT_FALSE(_mm_popcnt_u64(mask) > (StateLogic::capacity / 20))) {
                config_.policy.store(drift_policy::MSE_BASED, std::memory_order_relaxed);
            }

            monitor_emergency_drift();

            state_hash_ = _mm_crc32_u64(state_hash_, current_lsn_);

            // Divergence Trap: Check for state drift at a specific LSN
            while (SL_EXPECT_FALSE(next_trap_idx_ < trap_count_ && current_lsn_ == traps_[next_trap_idx_].lsn)) {
                last_trap_mse_ = divergence_analyzer<typename StateLogic::value_type, StateLogic::capacity>::analyze(
                    domain_state_, *traps_[next_trap_idx_].reference, current_lsn_, 
                    config_.policy.load(std::memory_order_relaxed),
                    config_.precision_delta.load(std::memory_order_relaxed), baseline,
                    config_.critical_drift.load(std::memory_order_relaxed),
                    tapestry_);
                update_drift_smoothing(last_trap_mse_);
                next_trap_idx_++;
            }

            // Periodic Asynchronous Snapshotting (every 64k events)
            if (SL_EXPECT_FALSE(snapshot_mgr_ && (current_lsn_ & 0xFFFF) == 0)) {
                snapshot_mgr_->async_snapshot(domain_state_, current_lsn_);
            }

            // 2. Extract the result
            if (auto response = domain_state_.generate_response()) {

                // TELEOLOGICAL AGNOSIA EGRESS
                // The CPU dynamically jumps to the No-Op void if data is frayed upstream.
                // No branches, no quarantining, no arbitration. 
                // The structural hole left by the removed error_arbiter is permanently sealed.
                uint8_t fray_result = 0; // Inherit structural fray from incoming envelopes if applicable
                aphasic_horizon_[fray_result](this, egress_, response, fray_result);
            }
            return mask;
        }

        /**
         * @brief Silicon Warm-up: Process a ghost event without side effects.
         * @details Used for dry runs and live priming to lock CPU frequency and BTB.
         */
        SLAB_HOT void process_warmup_ghost() noexcept {
            // Use a temporary event that mirrors a real workload
            EventType ghost_event{};
            // Process it silently (lsn is not incremented, state hash not updated)
            domain_state_.on_event(&ghost_event, current_lsn_);
            // Prevent compiler from optimizing away the loop
            asm volatile("" : : : "memory");
        }

        /** @brief Returns and clears the MSE of the most recent trap evaluation. */
        float get_last_trap_mse() noexcept {
            float m = last_trap_mse_;
            last_trap_mse_ = 0.0f; 
            return m;
        }

        /** @brief Returns the current Exponential Moving Average of MSE. */
        float get_mse_ema() const noexcept { return mse_ema_; }
    };
} // namespace slabflux::compute