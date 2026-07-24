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
 * ============================================================================* @file 02_fan_in_polling.cpp
 * @brief Fair Round-Robin event merging (Fan-in).
 */
#include <iostream>
#include "slabflux/core.hpp"

using namespace slabflux;
using namespace slabflux::core;

struct sensor_data {
    int sensor_id;
    double value;
    sensor_data(int id, double v) : sensor_id(id), value(v) {}
};

int main() {
    spsc_pool<sensor_data, 128> memory;
    spsc_conduit<sensor_data*, 64> eth0_track; // Network 1
    spsc_conduit<sensor_data*, 64> eth1_track; // Network 2
    event_gateway<spsc_pool<sensor_data, 128>> gateway(memory);

    // Poller that monitors two 64-slot conduits
    round_robin_poller<sensor_data, 2> poller;
    poller.bind_track(0, eth0_track);
    poller.bind_track(1, eth1_track);

    // Generate asymmetric load
    eth0_track.push(memory.make(0, 22.5));
    eth0_track.push(memory.make(0, 22.6));
    eth1_track.push(memory.make(1, 99.9));

    // Consumption (Fan-in)
    std::cout << "Polling events deterministically:\n";
    while (auto ev = poller.poll()) {
        gateway.consume(ev, [&](auto ref) {
            std::cout << "Read from Sensor " << ev->sensor_id
                      << " | Value: " << ev->value << "\n";
        });
    }
    std::cout << "All conduits empty.\n";

    return 0;
}
