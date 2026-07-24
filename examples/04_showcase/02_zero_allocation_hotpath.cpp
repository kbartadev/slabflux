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
 * ============================================================================* @file 02_zero_allocation_hotpath.cpp
 * @brief Enterprise Hot-Path: Zero-Allocation Dispatch & Const-Correctness
 */

#include <iostream>
#include <string_view>
#include "slabflux/meta.hpp"
#include "slabflux/core/pool.hpp"
#include "slabflux/core/pipeline.hpp"

using namespace slabflux;
using namespace slabflux::core;

// ============================================================================
// 1. DATA STACK (Zero-Copy & Native C++ Inheritance)
// ============================================================================

// L0: Standard FIX (Financial Information eXchange) header
struct fix_header {
    uint32_t seq_num;
    char     msg_type;
};

// L1: Payload (Native inheritance, no library-specific bloat)
struct new_order_single : fix_header {
    std::string_view symbol;
    double           price;
    uint64_t         internal_ts = 0; // Mutated later

    new_order_single(uint32_t seq, std::string_view sym, double p)
        : symbol(sym), price(p) {
        this->seq_num = seq;
        this->msg_type = 'D'; // 'D' = New Order Single
    }
};

// ============================================================================
// 2. DISPATCH STAGES (Read vs. Mutate via purely native C++ references)
// ============================================================================

// Stage 1: Sequence Validator (READ-ONLY – safe 'const' reference)
struct sequence_validator {
    // The Matrix automatically casts new_order_single to fix_header!
    SLAB_FORCE_INLINE bool on(const fix_header& ev) {
        if (ev.seq_num == 0) {
            std::cout << "[REJECT] Invalid sequence number: 0\n";
            return false; // Short-circuit
        }
        std::cout << "[VALID] Sequence #" << ev.seq_num << " accepted.\n";
        return true;
    }
};

// Stage 2: Ingress Timestamping (MUTATES – requires 'non-const' reference)
struct hardware_timestamper {
    // NOTE: Changed to non-const raw pointer T* for conduit usage
    SLAB_FORCE_INLINE void on(new_order_single* ev) {
        ev->internal_ts = 1680000000000ULL; // Simulated hardware clock
        std::cout << "[HW-TAG] Event stamped at " << ev->internal_ts << " ns.\n";
    }
};

// Stage 3: Matching Engine (READ-ONLY – for execution)
struct matching_engine {
    // NOTE: Changed to non-const raw pointer T* for conduit usage
    SLAB_FORCE_INLINE void on(new_order_single* ev) {
        std::cout << "[EXEC] Routing " << ev->symbol << " @ $" << ev->price
                  << " to matching core.\n";
    }
};

// ============================================================================
// 3. EXECUTION
// ============================================================================

int main() {
    std::cout << "=== Showcase 01: Zero-Allocation Hot Path ===\n\n";

    // O(1) LIFO allocator with RAII auto-reclamation
    spsc_pool<new_order_single, 1024> memory;

    sequence_validator   validator;
    hardware_timestamper stamper;
    matching_engine      engine;

    // Building the O(1) branchless pipeline
    pipeline<sequence_validator, hardware_timestamper, matching_engine>
        core_pipeline(validator, stamper, engine);

    std::cout << "--- Tick 1: Valid Order ---\n";
    // RAII guarantees memory returns to pool after dispatch!
    if (auto ev = memory.make(101, "AAPL", 150.25)) {
        core_pipeline.dispatch(ev); 
    }

    std::cout << "\n--- Tick 2: Corrupted Order ---\n";
    // Fails at Stage 1, pipeline aborts, memory instantly reclaimed!
    if (auto ev = memory.make(0, "TSLA", 200.00)) {
        core_pipeline.dispatch(ev); 
    }

    return 0;
}
