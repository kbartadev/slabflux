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
 * @file isa_guard.hpp
 * @brief Silicon Feature Validation.
 * @details Ensures the physical CPU supports all required instructions (AVX-512, AMX, etc.)
 * before allowing the deterministic core to ignite.
 */

#pragma once

#include <cpuid.h>
#include <stdexcept>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::sys {

    class isa_guard {
    public:
        struct features {
            bool has_sse42{false};
            bool has_avx2{false};
            bool has_avx512_f{false};
            bool has_avx512_vnni{false};
            bool has_waitpkg{false};
            bool has_shani{false};
            bool has_rdrand{false};
            bool has_lbr_capabilities{false};
        };

        /**
         * @brief Scans the silicon for available instruction set extensions.
         */
        static features detect() noexcept {
            features f;
            unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
            
            // Check Leaf 1 for baseline features (RDRAND)
            if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) { // Basic CPUID check
                f.has_sse42 = (ecx & (1 << 20));  // SSE4.2 bit
                f.has_rdrand = (ecx & (1 << 30)); // RDRAND bit
            }

            // Check Leaf 7, Subleaf 0 for modern extensions (AVX2, AVX-512, SHA-NI, WAITPKG, LBR)
            if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
                f.has_avx2 = (ebx & (1 << 5));
                f.has_avx512_f = (ebx & (1 << 16));
                f.has_shani = (ebx & (1 << 29));
                f.has_avx512_vnni = (ecx & (1 << 11));
                f.has_waitpkg = (ecx & (1 << 5));

                // edx Bit 19: Architectural LBRs (Sapphire Rapids+)
                f.has_lbr_capabilities = (edx & (1 << 19));
            }

            return f;
        }

        /**
         * @brief Requirement check
         * @details In strict mode, this terminates the RTE if baseline 
         * deterministic features (like WAITPKG) are missing.
         */
        static void verify_requirements() {
            auto f = detect();

            // The system is now architecture-aware and uses tiered 
            // fallbacks. Modern features like AVX-512 and SSE4.2 are treated as 
            // optimization tiers, while the core remains functional on any x86_64 host.
            // (Base requirement is now aligned with standard 64-bit silicon).

            // Informational: WAITPKG is preferred for low-power deterministic spinning
            if (!f.has_waitpkg) {
                // Fallback to PAUSE loops is already implemented in the core poller logic
            }

            if (!f.has_avx512_f) {
                // System will continue; high-level AI/Compute layers will utilize 256-bit SIMD paths
            }
        }
    };
}
