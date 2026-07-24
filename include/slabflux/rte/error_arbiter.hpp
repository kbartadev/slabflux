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
 *
 * @file event_arbiter.hpp
 * @brief Metaprogrammed Hierarchy Sorter and Prioritized Multi-Channel Polling.
 * @details Replaces generic priority-queue scheduling with a statically-resolved 
 * hierarchy. Synthesizes a branchless polling trace based on the structural 
 * sequence of template parameters to ensure deterministic O(1) arbitration.
 * Ensures Administrative commands and Temporal Ticks are processed with 
 * absolute priority over raw data frames to maintain system control.
 */
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <x86intrin.h>

namespace slabflux::rte {

    enum class error_severity : uint8_t {
        info = 0,
        warning,
        error,
        critical,
        fatal
    };

    enum class error_domain : uint8_t {
        network = 0,
        storage,
        compute,
        config,
        general
    };

    /**
     * @brief Fixed-size 32-byte error record.
     * @details Explicitly padded to ensure exactly two records fit in a 64-byte L1 cache line.
     */
    struct alignas(32) error_record {
        // Rationale: Provides sub-microsecond deterministic sequence anchoring without invoking syscalls (vDSO).
        uint64_t tsc;
        
        // Rationale: Tracks causal ordering across the distributed mesh.
        uint64_t lsn;
        
        // Rationale: Replaces variable-length strings, avoiding dynamic allocation and memcpy branches.
        uint64_t context;
        
        // Rationale: Encodes the specific fault type compactly for O(1) teleological routing.
        uint32_t code;
        
        // Rationale: Maps to categorical subsystem for high-level monitoring without string lookups.
        error_domain domain;
        
        // Rationale: Drives branchless escalation policies based on integer comparison.
        error_severity severity;
        
        // Rationale: Forces the structure to exactly 32 bytes, allowing 2 records per 64-byte L1 cache line.
        uint16_t pad_;
    };

    /**
     * @brief Deterministic, zero-allocation SPSC Error Arbiter.
     */
    template <size_t Capacity = 1024>
    class alignas(64) error_arbiter {
    public:
        static constexpr size_t capacity = Capacity;
        static_assert((capacity & (capacity - 1)) == 0, "Capacity must be a power of two");

        // Rationale: Eliminates virtual dispatch and type-erasure overhead inherent in std::function.
        using escalation_fn = void(*)(const error_record&);

    private:
        // --- Producer Hot State ---
        // Rationale: Isolating the producer's write cursor alongside its cached read cursor in a single 
        // 64-byte aligned struct entirely prevents False Sharing (RFO stalls) when the consumer reads the queue.
        struct alignas(64) producer_state {
            std::atomic<uint64_t> write_cursor{0};
            uint64_t cached_read_cursor{0};
        };
        producer_state prod_;

        // --- Consumer Hot State ---
        // Rationale: Physically segregates the consumer's atomic state onto its own dedicated L1 cache line.
        struct alignas(64) consumer_state {
            std::atomic<uint64_t> read_cursor{0};
        };
        consumer_state cons_;

        // --- System Panic State ---
        // Rationale: Fast, wait-free global panic indicator. Segregated to prevent false sharing 
        // when read by external telemetry or watchdog threads.
        alignas(64) std::atomic<bool> panic_flag_{false};

        // --- Configuration & Callbacks (Cold State) ---
        alignas(64) escalation_fn escalation_cb_{nullptr};
        error_severity escalation_threshold_{error_severity::critical};

        // --- Pre-allocated Ring Buffer ---
        // Rationale: Eliminates dynamic heap allocation. Guarantees the entire quarantine 
        // ring resides in pre-faulted contiguous memory entirely avoiding TLB misses on insertion.
        alignas(64) std::array<error_record, capacity> ring_buffer_{};

    public:
        error_arbiter() noexcept = default;
        ~error_arbiter() noexcept = default;

        // Non-copyable, non-movable
        error_arbiter(const error_arbiter&) = delete;
        error_arbiter& operator=(const error_arbiter&) = delete;

        /**
         * @brief Configures the threshold and callback for severe errors.
         */
        void set_escalation_policy(error_severity threshold, escalation_fn cb) noexcept {
            escalation_threshold_ = threshold;
            escalation_cb_ = cb;
        }

        /**
         * @brief Branchless, wait-free error recording.
         */
        void record_error(error_domain domain, uint32_t code, error_severity severity, 
                          uint64_t lsn, uint64_t context = 0) noexcept {
            
            const uint64_t current_tsc = __rdtsc();
            const uint64_t w_idx = prod_.write_cursor.load(std::memory_order_relaxed);
            
            // Re-evaluate cached read cursor only if capacity appears exhausted
            if (w_idx - prod_.cached_read_cursor >= capacity) [[unlikely]] {
                prod_.cached_read_cursor = cons_.read_cursor.load(std::memory_order_acquire);
                if (w_idx - prod_.cached_read_cursor >= capacity) {
                    // Teleological Agnosia: Quarantine ring full, drop fault to preserve determinism.
                    return;
                }
            }

            // O(1) Branchless index calculation
            const size_t slot = w_idx & (capacity - 1);
            
            error_record& rec = ring_buffer_[slot];
            rec.tsc      = current_tsc;
            rec.lsn      = lsn;
            rec.context  = context;
            rec.code     = code;
            rec.domain   = domain;
            rec.severity = severity;

            // Publish to consumer
            prod_.write_cursor.store(w_idx + 1, std::memory_order_release);

            if (severity >= error_severity::fatal) [[unlikely]] {
                panic_flag_.store(true, std::memory_order_release);
            }

            // Out-of-band escalation evaluation
            if (severity >= escalation_threshold_ && escalation_cb_ != nullptr) [[unlikely]] {
                escalation_cb_(rec);
            }
        }

        /**
         * @brief Fast-path check for global system panic.
         */
        [[nodiscard]] bool has_panicked() const noexcept {
            return panic_flag_.load(std::memory_order_acquire);
        }

        /**
         * @brief Consumer extraction.
         */
        bool try_pop(error_record& out_record) noexcept {
            const uint64_t r_idx = cons_.read_cursor.load(std::memory_order_relaxed);
            const uint64_t w_idx = prod_.write_cursor.load(std::memory_order_acquire);

            if (r_idx == w_idx) {
                return false;
            }

            const size_t slot = r_idx & (capacity - 1);
            out_record = ring_buffer_[slot];

            cons_.read_cursor.store(r_idx + 1, std::memory_order_release);
            return true;
        }
    };

} // namespace slabflux::rte
