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

#include "slabflux/core/sf_node_ctx.hpp"
#include "slabflux/compute/vector_lane_engine.hpp"
#include "slabflux/core/backpressure_valve.hpp"
#include "slabflux/core/chip_telemetry.hpp"
#include "slabflux/net/nexus_connector.hpp"

int main() {
    // 1. Physical pinning
    mlockall(MCL_CURRENT | MCL_FUTURE);

    // 2. Chip components
    sf_node_ctx context;
    vector_lane_256<64> engine;
    backpressure_valve valve(1024); // stall at 1024 LSN
    chip_telemetry telemetry;

    // 3. The Bridge with feedback
    nexus_connector<market_signal, 1024> bridge(context, engine, "truth.log");

    // 4. Ingress
    slabflux::core::pool<char> rx_pool(4096);
    matrix_nexus<decltype(bridge)> nexus(rx_pool, bridge);

    std::cout << "[SLABFLUX] Chip is ONLINE." << std::endl;

    // --- GENESIS LOOP ---
    while (true) [[likely]] {
        // If the valve is open, read from the wire
        if (!valve.is_stalled()) [[likely]] {
            nexus.poll_and_execute();
        }

        // Update the valve based on the horizon
        valve.update(context.current_lsn, context.committed_lsn);

        // Update telemetry
        telemetry.record_arrival(context.committed_lsn);
    }
}
