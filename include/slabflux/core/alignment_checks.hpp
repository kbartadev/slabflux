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

#include <cstddef>

namespace slabflux::core {

    // Helper to get absolute difference of two size_t values.
    consteval size_t abs_diff(size_t a, size_t b) {
        return a > b ? a - b : b - a;
    }

    /**
     * @brief C++20 Consteval Layout Verifier.
     * @details Provides compile-time checks for memory layout invariants,
     * replacing macro-based boilerplate with type-safe consteval functions.
     */
    struct layout_verifier {
        /**
         * @brief Verifies that a member starts at a 64-byte cache line boundary.
         */
        template <size_t Offset>
        static consteval void enforce_cache_aligned() {
            static_assert(Offset % 64 == 0, "Structural Breach: Member is not cache-line aligned!");
        }

        /**
         * @brief Verifies that two class members reside on different 64-byte cache lines.
         */
        template <size_t Offset1, size_t Offset2>
        static consteval void enforce_isolation() {
            static_assert(abs_diff(Offset1, Offset2) >= 64,
                          "Structural Breach: Cache-line isolation failure between members!");
        }

        /**
         * @brief Verifies that a member is located at a specific byte offset.
         */
        template <size_t ActualOffset, size_t ExpectedOffset>
        static consteval void enforce_offset() {
            static_assert(ActualOffset == ExpectedOffset, "Structural Breach: Member is not at required offset!");
        }
    };

    /**
     * @brief Legacy Alignment Macros (Bridge for industrial test suites).
     * @details Maps legacy preprocessor checks to the C++20 Consteval Layout Verifier.
     * These are provided to maintain compatibility with existing validation logic 
     * while leveraging the bit-perfect verifier.
     */

    #define SLAB_ENFORCE_OFFSET(Member, Offset, Type) \
        static_assert([]() consteval { \
            slabflux::core::layout_verifier::enforce_offset<__builtin_offsetof(Type, Member), Offset>(); \
            return true; \
        }(), "Structural Breach: Member " #Member " is not at required offset " #Offset)

    #define SLAB_ENFORCE_CACHE_ALIGNED(Member, Type) \
        static_assert([]() consteval { \
            slabflux::core::layout_verifier::enforce_cache_aligned<__builtin_offsetof(Type, Member)>(); \
            return true; \
        }(), "Structural Breach: Member " #Member " is not cache-line aligned")

    #define SLAB_ENFORCE_ISOLATION(Member1, Member2, Type) \
        static_assert([]() consteval { \
            slabflux::core::layout_verifier::enforce_isolation<__builtin_offsetof(Type, Member1), __builtin_offsetof(Type, Member2)>(); \
            return true; \
        }(), "Structural Breach: Cache-line isolation failure between " #Member1 " and " #Member2)

} // namespace slabflux::core
