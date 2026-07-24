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
 * ============================================================================* @file sovereign_thread.hpp
 * @brief Automated application of all hardware-level execution protections.
 */

#pragma once

#include "slabflux/sys/isa_guard.hpp"
#include "slabflux/sys/signal_shield.hpp"
#include "slabflux/sys/fpu_shield.hpp"
#include "slabflux/sys/power_governor.hpp"
#include "slabflux/core/hardware_topology.hpp"

namespace slabflux::sys {

    /**
     * @brief The Sovereign Thread Bootstrapper.
     * @details Actively enforces the system's vast array of hardware-level 
     * protections on thread ignition. By calling this, a standard OS thread 
     * sheds its OS context and becomes a deterministic execution unit.
     */
    class sovereign_thread {
        fpu_shield shield_;
        power_governor gov_;

    public:
        explicit sovereign_thread(int target_core_id = -1) {
            // 1. Verify exact silicon requirements
            isa_guard::verify_requirements();

            // 2. Lock thread to specific physical silicon
            if (target_core_id >= 0) {
                core::hardware_topology::pin_thread(target_core_id);
            }

            // 3. Drop all standard OS signal interruptions to prevent jitter
            signal_shield::block_all_on_current_thread();

            // 4 & 5. Sanitize FPU and prime vector units
            gov_.prime_vector_units();
        }
    };

} // namespace slabflux::sys