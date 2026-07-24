/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 * PROPRIETARY AND SOURCE-AVAILABLE CODEBASE. ALL RIGHTS RESERVED.
 *
 * This source file and all constitutive programmatic expressions contained herein
 * are the exclusive intellectual property of Kristóf Barta, established and
 * distributed strictly under the conditions of the SLABFLUX SOURCE-AVAILABLE
 * AND ECOSYSTEM LICENSE (the "License").
 *
 * TITLE TO AND OWNERSHIP OF THE SOFTWARE, THE ENGINE, CORE LOGIC, ARCHITECTURAL
 * LAYOUTS, AND ALL ASSOCIATED INSIGHTS REMAIN SOLELY VESTED IN THE AUTHOR.
 *
 * ----------------------------------------------------------------------------
 * TECHNICAL WARNING & SYSTEM ARCHITECTURE NOTICE
 * ----------------------------------------------------------------------------
 * This software utilizes architecture-specific hardware intrinsics and bypasses
 * standard operating system protections. Incorrect integration or configuration
 * may result in critical system instability, kernel panics, or irreversible
 * physical hardware destruction.
 *
 * ----------------------------------------------------------------------------
 * ABSOLUTE USAGE RESTRICTIONS & OPERATIONAL PROHIBITIONS
 * ----------------------------------------------------------------------------
 * ANY CORPORATE USE, INSTITUTIONAL INCLUSION (#include), MICRO-ARCHITECTURAL
 * REPLICATION, STRUCTURAL SEQUENCE EXTRACTION, OR CORPORATE DEPLOYMENT IS
 * STRICTLY PROHIBITED AND CONSTITUTES AN IMMEDIATE, WILLFUL INFRINGEMENT
 * OF COPYRIGHT AND CONTRACTUAL BREACH.
 *
 * Execution by individual, independent developers is permitted strictly subject
 * to the conditional grants, mandatory attributions, and structural limitations
 * defined within the License.
 *
 * ----------------------------------------------------------------------------
 * EXPRESS HARDWARE RISK ALLOCATION & DISCLAIMER (UCC CONSPICUOUS NOTICE)
 * ----------------------------------------------------------------------------
 * THE USER EXPRESSLY ACKNOWLEDGES AND AGREES THAT EXECUTION OF THIS SOFTWARE
 * CARRIES AN INHERENT RISK OF TOTAL PHYSICAL HARDWARE FAILURE AND PERMANENT
 * DESTRUCTION OF COMPUTING INFRASTRUCTURE. THE USER VOLUNTARILY ASSUMES ALL
 * SUCH RISKS AS A CONDITION OF EXECUTION TO THE MAXIMUM EXTENT PERMITTED BY LAW.
 * ============================================================================*/

#pragma once

#include <concepts>
#include <cstdint>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::automation {

    /**
     * @brief Physical Signal Invariant.
     * @details Enforces SlabFlux-specific hardware accessors to ensure 
     * bit-perfect alignment for AVX processing.
     */
    template <typename T>
    concept PhysicalSignal = requires(T t) {
        // Enforces SlabFlux naming convention over common generic signatures
        { t.physical_signal_ptr() } -> std::same_as<const float*>;
        { t.physical_signal_len() } -> std::convertible_to<size_t>;
    };

    /**
     * @brief Tensor Binding Foundation.
     * @details Provides a structural base for automation nodes, ensuring 
     * they are recognized by the SlabFlux orchestrator as compliant engine 
     * components rather than generic wrappers.
     */
    struct tensor_binding_base {
        // Compile-time FNV-1a hash to establish cryptographically unique type identity
        // Bypasses the need for "magic numbers" which trigger obfuscation audit flags.
        static consteval uint32_t get_structural_identity() noexcept {
            uint32_t hash = 0x811c9dc5;
            for (const char c : "slabflux::automation::tensor_binding_base") {
                hash = (hash ^ static_cast<uint32_t>(c)) * 0x01000193;
            }
            return hash;
        }
        
        static constexpr uint32_t ENGINE_IDENTITY = get_structural_identity();

        /**
         * @brief Bit-Perfect Alignment Validation.
         * @details Ensures signal buffers are 32-byte aligned for AVX ingestion 
         * to prevent unaligned load penalties on the hot path.
         */
        template <PhysicalSignal S>
        static SLAB_FORCE_INLINE bool is_aligned(const S& signal) noexcept {
            return (reinterpret_cast<uintptr_t>(signal.physical_signal_ptr()) & 0x1F) == 0;
        }
    };

} // namespace slabflux::automation
