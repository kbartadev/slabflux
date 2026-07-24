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

#include "slabflux/meta.hpp"
#include "slabflux/core/physical_layout.hpp"
#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/core/pool.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include <iostream>
#include "slabflux/core/mpmc_conduit.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"
#include "slabflux/core/pipeline.hpp"
#include "slabflux/core/bound_sink.hpp"
#include "slabflux/bridge/round_robin_switch.hpp"
#include "slabflux/bridge/round_robin_poller.hpp"
#include "slabflux/core/runtime_domain.hpp"
#include "slabflux/sys/stack_guard.hpp"
#include "slabflux/sys/fpu_shield.hpp"
#include "slabflux/sys/power_governor.hpp"
#include "slabflux/core/event_gateway.hpp"
#include "slabflux/net/demux_gateway.hpp"
#include "slabflux/rte/environment.hpp"
#include "slabflux/rte/ignition_manifest.hpp"

// Forward include conduit.hpp to ensure the 'conduit' alias is established
#include "slabflux/core/conduit.hpp"

namespace slabflux {
    using core::CACHE_LINE_SIZE;
    using core::pool;
    using core::local_pool;
    using core::spsc_pool;
    using core::mpsc_pool;
    using core::asymmetric_pool;
    using core::mpmc_pool;
    using core::conduit; // This now correctly maps to core::spsc_conduit
    using core::spsc_ring_conduit;
    using core::spsc_conduit;
    using core::mpmc_conduit;
    using core::pipeline;
    using core::bound_sink;
    using bridge::round_robin_switch;
    using bridge::round_robin_poller;
    using core::runtime_domain;
    using core::event_gateway;
    using net::demux_gateway;

    template<typename Logic>
    class engine {
    public:
        /**
         * @brief The absolute start of the universe.
         */
        void ignite() {
            // 1. Verify physical environment
            rte::ignition_manifest::verify_all();
            
            // 2. Lock memory and shield cores
            sys::stack_guard::pre_fault_stack(2048);
            sys::power_governor::lock_c_states();
            
            // 3. Warm up the FPU and SIMD
            sys::fpu_shield::engage();
            sys::power_governor::warm_up_simd();
            
            std::cout << "[SYSTEM] CORE IGNITED. Awaiting control...\n";
            
            // 4. Enter the O(1) loop
            // environment.run();
        }
    };
}
