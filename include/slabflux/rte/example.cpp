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
#include "error_arbiter.hpp"
#include <iostream>

using namespace slabflux::rte;

void escalation_handler(const error_record& rec) {
    std::cerr << "[ESCALATION] Critical fault detected in domain " 
              << static_cast<int>(rec.domain) << ". Code: " 
              << rec.code << " | LSN: " << rec.lsn << "\n";
}

int main() {
    // 1. Initialize deterministic Arbiter
    error_arbiter<2048> arbiter; // Now utilizing the template parameter

    // 2. Register a Fatal/Critical escalation callback
    arbiter.set_escalation_policy(error_severity::critical, escalation_handler);

    // 3. Record errors in hot-path without dynamic memory or formatting
    arbiter.record_error(error_domain::network, 100, error_severity::warning, 1);

    // 4. Trigger an escalation event with payload context
    arbiter.record_error(error_domain::compute, 503, error_severity::fatal, 2, 0xFFFF);

    // 5. Query recent errors for diagnostics
    error_record err;
    std::cout << "\n--- Recent Error Dump ---\n";
    while (arbiter.try_pop(err)) {
        std::cout << "TSC: " << err.tsc << " | Code: " << err.code << " | LSN: " << err.lsn << "\n";
    }

    return 0;
}