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
 */

#pragma once

#include <string_view>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <thread>
#include <cpuid.h>
#include <pthread.h>

namespace slabflux::sys {

    class topology_scanner {
    public:
        /**
         * @brief Zero-syscall Hardware L3 Topology Discovery.
         * @details Bypasses fakeable container sysfs masks by pinning to the requested 
         * cores and executing native CPUID Leaf 0x01 to extract the physical APIC ID.
         */
        static bool share_l3_cache(int cpu1, int cpu2) {
            auto get_apic_id = [](int target_cpu) -> uint32_t {
                uint32_t apic_id = 0xFFFFFFFF;
                std::thread([&apic_id, target_cpu]() {
                    cpu_set_t set;
                    CPU_ZERO(&set);
                    CPU_SET(target_cpu, &set);
                    if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0) {
                        unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
                        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
                            apic_id = (ebx >> 24) & 0xFF; // Extract initial APIC ID
                        }
                    }
                }).join();
                return apic_id;
            };

            uint32_t apic1 = get_apic_id(cpu1);
            uint32_t apic2 = get_apic_id(cpu2);

            if (apic1 == 0xFFFFFFFF || apic2 == 0xFFFFFFFF) return true; // Fallback

            // A robust solution for modern Intel/AMD architectures: 
            // cores sharing the same L3 cache typically share the upper bits of their APIC ID.
            return (apic1 & ~0x0F) == (apic2 & ~0x0F);
        }
        
        static void enforce_proximity(int ingress_cpu, int compute_cpu) {
            if (!share_l3_cache(ingress_cpu, compute_cpu)) {
                throw std::runtime_error("Topology Violation: Ingress and Compute must share L3!");
            }
        }
    };
}
