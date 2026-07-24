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

#include "slabflux/rte/error_arbiter.hpp"
#include "slabflux/security/kinetic_inscription.hpp"
#include <x86intrin.h>
#include <iostream>
#include <thread>

namespace slabflux::mgmt {

    /**
     * @brief The System Nanoscope.
     * @details Structural Honesty. A separate, non-isolated thread 
     * that "observes" the Chip's health without touching its cache lines.
     */
    class nanoscope {
    public:
        template <size_t ArbiterCapacity = 1024>
        SLAB_COLD void monitor_health(rte::error_arbiter<ArbiterCapacity>* arbiter = nullptr,
                                      const security::panoptic_reticle* reticle = nullptr) noexcept {
            uint32_t last_kinetic_lsn = 0;

            while (true) {
                // 1. Check Arbiter for software-routed faults
                if (arbiter) {
                    if (arbiter->has_panicked()) [[unlikely]] {
                        std::cerr << "[CRITICAL] EMERGENCY STOP: SYSTEM PANICKED VIA ARBITER!\n";
                        // Trigger Hardware Fence (e.g., NIC reset)
                        break;
                    }

                    rte::error_record fault;
                    while (arbiter->try_pop(fault)) {
                        std::cout << "[FAULT] LSN: " << fault.lsn 
                                  << " | Code: " << fault.code 
                                  << " | Severity: " << static_cast<int>(fault.severity) << "\n";
                    }
                }

                // 2. Check Kinetic Inscription Reticle for Hardware LBR faults
                if (reticle) {
                    uint8_t error_code = 0;
                    uint32_t lsn = 0;
                    if (reticle->harvest_anomaly(error_code, lsn)) {
                        // MSRs don't clear on read, so we filter by LSN to prevent duplicate alerts
                        if (lsn != last_kinetic_lsn) {
                            last_kinetic_lsn = lsn;
                            std::cerr << "[KINETIC ANOMALY] LSN: " << lsn 
                                      << " | Hardware Fray Code: 0x" 
                                      << std::hex << static_cast<int>(error_code) << std::dec << "\n";
                            
                            if (error_code == 0xDE || error_code == 0x0D) [[unlikely]] {
                                std::cerr << "[CRITICAL] EMERGENCY STOP: SILICON DIVERGENCE DETECTED!\n";
                                break; // Halt nanoscope on fatal silicon divergence
                            }
                        }
                    }
                }

                // Replaces standard OS sleep with a hardware-native TSC bounded yield.
                // Prevents the Linux CFS scheduler from penalizing the thread priority while idling.
                uint64_t target_tsc = __rdtsc() + 30'000'000ULL; // ~10ms at 3GHz
                while (__rdtsc() < target_tsc) {
                    _mm_pause();
                }
            }
        }
    };
}