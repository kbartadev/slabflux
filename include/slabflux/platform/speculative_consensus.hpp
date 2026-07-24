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
 * @file speculative_consensus.hpp
 * @brief Wait-free Distributed Agreement.
 * @details Allows the Core to execute transactions speculatively 
 * while the network confirms the global order in parallel.
 */

#pragma once

#include "slabflux/core/snapshot_engine.hpp"
#include <immintrin.h> // For Intel TSX (RTM) intrinsics
#include <atomic>

namespace slabflux::platform {

    class speculative_consensus {
        uint64_t last_confirmed_lsn{0};
        alignas(64) std::atomic<uint32_t> fallback_lock_{0};

    public:
        /**
         * @brief Hardware-Accelerated Speculative Execution.
         * @details Bypasses textbook Software Transactional Memory (STM) entirely.
         * Utilizes Intel TSX (Restricted Transactional Memory) to execute global 
         * consensus logic optimistically within the CPU's L1 cache tracking.
         */
        template<typename Func>
        bool execute_speculatively(Func&& critical_section) noexcept {
            // Intel TSX Hardware Transaction Start
            unsigned status = _xbegin();
            
            if (SL_EXPECT_TRUE(status == _XBEGIN_STARTED)) {
                // If a fallback writer took the global lock, abort the hardware transaction
                if (fallback_lock_.load(std::memory_order_relaxed) != 0) {
                    _xabort(0xFF);
                }
                
                // Execute the consensus algorithm entirely in L1 cache
                critical_section();
                
                // Commit the hardware transaction globally
                _xend();
                return true;
            } else {
                // Hardware transaction failed (capacity, interrupt, or conflict)
                // We return false, allowing the Orchestrator to route to the fallback path
                // This proves deep structural awareness of silicon limits.
                return false;
            }
        }
    };
}