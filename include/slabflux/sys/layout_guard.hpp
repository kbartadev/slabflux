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
 * @file layout_guard.hpp
 * @brief Compile-time Structural Fingerprinting.
 * @details Generates a unique 64-bit ID based on the StateLogic memory layout.
 * Prevents loading incompatible snapshots after code changes.
 */

#pragma once

#include <cstdint>
#include <stdexcept>
#include <new> // For hardware_constructive_interference_size
#include <bit> // For std::endian

namespace slabflux::sys {

    template<typename T>
    struct layout_guard {
    private:
        // Compile-time FNV-1a hash engine for structural type identification
        static consteval uint64_t fnv1a_64(const char* str) noexcept {
            uint64_t hash = 0xcbf29ce484222325ULL;
            while (*str) {
                hash ^= static_cast<uint64_t>(*str++);
                hash *= 0x100000001b3ULL;
            }
            return hash;
        }

        // Compiler-specific deep type reflection (extracts namespace, class name, and template args)
#if defined(__clang__) || defined(__GNUC__)
        static constexpr const char* type_name = __PRETTY_FUNCTION__;
#else
        static constexpr const char* type_name = __FUNCSIG__;
#endif

    public:
        /**
         * @brief A unique deterministic hash based on exact type signature, size, and alignment.
         * @details Silicon-Bound ABI Fingerprinting.
         * Differentiates from generic ABI checks by mathematically fusing the target 
         * micro-architecture's physical characteristics (Cache line size, Endianness) 
         * into the compile-time layout hash. Prevents proprietary state-files from 
         * being loaded on unauthorized silicon configurations.
         */
        static constexpr uint64_t signature =
            fnv1a_64(type_name) 
            ^ (static_cast<uint64_t>(sizeof(T)) << 32) 
            ^ (static_cast<uint64_t>(alignof(T)) << 16)
            ^ (static_cast<uint64_t>(std::hardware_constructive_interference_size) << 48)
            ^ (std::endian::native == std::endian::little ? 0x00000000011771EULL : 0x000000000000B16ULL);

        static void verify(uint64_t stored_signature) {
            if (stored_signature != signature) {
                throw std::runtime_error("Snapshot Incompatibility: Binary layout mismatch!");
            }
        }
    };
}
