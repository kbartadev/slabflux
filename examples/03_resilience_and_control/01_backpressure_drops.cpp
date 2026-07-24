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
 * ============================================================================* @file 01_backpressure_drops.cpp
 * @brief Short-circuiting execution in the middle of the O(1) pipeline.
 */
#include <iostream>
#include "slabflux/meta.hpp"
#include "slabflux/core/pipeline.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

using namespace slabflux;
using namespace slabflux::core;

struct order { double qty; order(double q) : qty(q) {} };

// Gatekeeper that can stop the pipeline natively via boolean short-circuiting
struct risk_firewall {
    // NOTE: Changed to non-const raw pointer T* for conduit usage
    SLAB_FORCE_INLINE bool on(order* ev) {
        if (ev->qty > 10000.0) { std::cout << "[FIREWALL] Order too large (" << ev->qty << "). DROPPED.\n"; return false; }
        return true;
    }
};

struct execution_engine {
    SLAB_FORCE_INLINE void on(order* ev) { std::cout << "[EXEC] Order executed for qty: " << ev->qty << "\n"; }
};

int main() {
    std::cout << "=== Showcase 01: Pipeline Short-Circuiting (Zero-Allocation) ===\n\n";

    risk_firewall firewall;
    execution_engine engine;
    pipeline<risk_firewall, execution_engine> pipe(firewall, engine);

    // Pure stack execution (Zero heap overhead)
    std::cout << "Sending small order...\n";
    pipe.dispatch(order(50.0));

    std::cout << "\nSending massive order...\n";
    // Will be halted by the firewall, engine never triggered.
    pipe.dispatch(order(15000.0));

    std::cout << "\nSending medium order...\n";
    pipe.dispatch(order(500.0));

    return 0;
}
