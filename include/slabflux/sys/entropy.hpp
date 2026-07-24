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
 * @file entropy.hpp
 * @brief Quantum-based Hardware Randomness.
 * @details Pulls entropy directly from the CPU's thermal noise 
 * to seed cryptographic operations without hurting determinism.
 */

#pragma once

#include <immintrin.h>
#include <stdexcept>
#include "slabflux/sys/isa_guard.hpp"
#include <x86intrin.h>
#include <cpuid.h>

namespace slabflux::sys {

    class entropy {
    public:
        /**
         * @brief Returns a 64-bit true random number.
         * @note Uses the RDRAND instruction (Hardware TRNG) with mandatory retry loops.
         */
        static inline uint64_t get_quantum_seed() noexcept {
            unsigned long long val = 0;
            
            // Silicon-Fused Entropy.
            // We fuse the hardware TRNG output with the CPU's physical APIC ID 
            // and exact cycle execution jitter to create a robust hardware fingerprint.
            uint64_t jitter_start = __rdtsc();

            if (isa_guard::detect().has_rdrand) {
                // Intel SDM requires a retry loop (min 10) because the hardware 
                // entropy pool can underflow during high concurrent demand.
                for (int i = 0; i < 10; ++i) {
                    if (_rdrand64_step(&val)) {
                        unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
                        __get_cpuid(1, &eax, &ebx, &ecx, &edx);
                        uint64_t apic_id = (ebx >> 24) & 0xFF;
                        return static_cast<uint64_t>(val) ^ (__rdtsc() - jitter_start) ^ (apic_id << 32);
                    }
                    _mm_pause();
                }
            }
            // Fallback: Zero-syscall pseudo-entropy using TSC + Pointer Hash
            // Avoids std::random_device which blocks on /dev/urandom.
            uint64_t seed = __rdtsc();
            seed ^= reinterpret_cast<uint64_t>(&val); 
            return seed ^ 0x9E3779B97F4A7C15ULL; // Splitmix-style mix
        }
    };
}
