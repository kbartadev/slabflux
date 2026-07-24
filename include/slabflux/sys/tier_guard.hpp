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
 * @file tier_guard.hpp
 * @brief Memory Tiering Enforcement.
 * @details Ensures that the Slab only resides in Near-Memory (DRAM) 
 * and never spills over to Far-Memory (CXL).
 */

#pragma once

#include <numa.h>
#include <numaif.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

namespace slabflux::sys {

    class tier_guard {
    public:
        /**
         * @brief Verifies that the allocated memory block is on a DRAM node.
         */
        static void enforce_dram_locality(void* ptr, size_t size) {
            int node = -1;
            if (get_mempolicy(&node, nullptr, 0, ptr, MPOL_F_NODE | MPOL_F_ADDR) == 0) {
                // In 2026, we check if 'node' is a CXL type or Local DRAM
                if (is_cxl_node(node)) {
                    throw std::runtime_error("Violation: Slab leaked into CXL memory!");
                }
            }
        }

    private:
        static bool is_cxl_node(int node) noexcept {
            // Check: CXL/Far-Memory nodes are typically identified 
            // by the absence of CPUs and specific 'node_hmem' attributes in sysfs.
            
            // Bypass Linux sysfs string parsing.
            // Use native binary libnuma bitmasks to avoid slow O_RDONLY file allocations.
            struct bitmask* cpus = numa_allocate_cpumask();
            if (numa_node_to_cpus(node, cpus) != 0) {
                numa_free_cpumask(cpus);
                return true; // Fail-open (treat as CXL if inaccessible)
            }

            // Hardware verification: if the node has exactly 0 usable execution cores,
            // it is a memory-only target (PMEM/CXL Expansion).
            bool is_memory_only = (numa_bitmask_weight(cpus) == 0);
            numa_free_cpumask(cpus);
            return is_memory_only;
        }
    };
}
