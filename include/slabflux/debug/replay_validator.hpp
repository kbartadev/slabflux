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
 * @file replay_validator.hpp
 * @brief Deep State Replay Implementation.
 */

#pragma once

#include "slabflux/core/snapshot_engine.hpp"
#include "slabflux/core/journal_reader.hpp"
#include "slabflux/core/logic_engine.hpp"
#include <nmmintrin.h> // SSE4.2 for hardware CRC32

namespace slabflux::debug {

    class replay_validator {
    private:
        core::logic_engine& engine_;
        core::journal_reader& journal_;
        core::snapshot_engine& snapshot_mgr_;

    public:
        replay_validator(core::logic_engine& e, core::journal_reader& j, core::snapshot_engine& s)
            : engine_(e), journal_(j), snapshot_mgr_(s) {}

        /**
         * @brief Bit-level validation loop.
         */
        bool validate_window(uint64_t start_lsn, uint64_t end_lsn) {
            // 1. Restore the original state
            snapshot_mgr_.restore(engine_.get_state());

            uint64_t current_lsn = start_lsn;

            // 2. Feed the journal entries into the logic engine
            while (current_lsn < end_lsn) {
                auto* entry = journal_.get_entry(current_lsn);
                if (!entry) break;

                // Save the original state hash for comparison
                uint64_t original_hash = entry->recorded_state_hash;

                // Re-run the logic with the historical input
                engine_.process_event(entry->payload);

                // 3. Bit-level comparison
                uint32_t expected_hash = journal_.get_recorded_hash(start_lsn);
                uint32_t actual_hash = engine_.get_integrity_hash();

                if (expected_hash != actual_hash) {
                    arbiter_.signal_fault(start_lsn, 0xBAAD_57A7, fault_severity::CRITICAL);
                }

                current_lsn++;
            }

            return true; // Past and present match bit-for-bit.
        }
    };
}
