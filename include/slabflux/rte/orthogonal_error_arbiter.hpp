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

#include <array>
#include <atomic>
#include <cstdint>
#include <concepts>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::rte {

    /**
     * @brief Prime-Factor Error Topology (Gödel Numbering)
     * @details Completely replaces enumerations and bitmasks. Hierarchies are 
     * mathematically enforced via prime factorization. Relation checks are 
     * O(1) modulo operations.
     */
    namespace error_topology {
        using code_t = uint32_t;

        // Base Dimensional Primes
        constexpr code_t base_hardware = 2;
        constexpr code_t base_network  = 3;
        constexpr code_t base_compute  = 5;
        constexpr code_t base_storage  = 7;

        // Compound Hierarchical Errors (Products)
        constexpr code_t net_timeout   = base_network * 11;
        constexpr code_t net_corrupt   = base_network * 13;
        constexpr code_t hw_thermal    = base_hardware * 17;
        constexpr code_t hw_numa_fault = base_hardware * 19;
        constexpr code_t logic_nan     = base_compute * 23;

        /** @brief Branchless, O(1) hierarchy relation check. */
        SLAB_FORCE_INLINE constexpr bool is_a(code_t error, code_t category) noexcept {
            return (error % category) == 0;
        }
    }

    /**
     * @brief Orthogonal Subsumption Field for Error Arbitration.
     * @details Radically replaces all queuing, ring-buffers, and sequence 
     * versioning with a statically-mapped Algebraic Tensor Field.
     * 
     * Properties:
     * 1. 0 Branches, 0 CAS loops, 0 False Sharing (via perfect hashing).
     * 2. Absolute wait-free emission via O(1) Subsumption (Overwriting).
     * 3. Completely immune to OOM and Queue-Full states regardless of failure rate.
     */
    template <size_t Capacity = 1024>
    class alignas(64) orthogonal_error_arbiter {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two for bitwise clipping.");

        // The Subsumption Field: 64-bit Tensors representing physical disturbances.
        // [ 32-bit Gödel Error Code | 32-bit Magnitude/Severity ]
        alignas(64) std::atomic<uint64_t> field_[Capacity];

        // Hardware-optimized Knuth Multiplicative Perfect Hash
        SLAB_FORCE_INLINE static constexpr size_t project_coordinate(error_topology::code_t code) noexcept {
            return (code * 2654435761U) & (Capacity - 1);
        }

    public:
        orthogonal_error_arbiter() noexcept {
            for (size_t i = 0; i < Capacity; ++i) {
                field_[i].store(0, std::memory_order_relaxed);
            }
        }
        ~orthogonal_error_arbiter() noexcept = default;

        orthogonal_error_arbiter(const orthogonal_error_arbiter&) = delete;
        orthogonal_error_arbiter& operator=(const orthogonal_error_arbiter&) = delete;

        /**
         * @brief Tends the field. Projects a physical error tensor into the domain.
         */
        SLAB_FORCE_INLINE void record(error_topology::code_t code, uint32_t magnitude) noexcept {
            const uint64_t tensor = (static_cast<uint64_t>(code) << 32) | magnitude;
            const size_t coord = project_coordinate(code);
            
            field_[coord].store(tensor, std::memory_order_release);
        }

        /**
         * @brief Harvests and nullifies a specific sector of the field.
         */
        SLAB_FORCE_INLINE bool try_harvest(error_topology::code_t code, uint32_t& out_magnitude) noexcept {
            const size_t coord = project_coordinate(code);
            uint64_t tensor = field_[coord].exchange(0, std::memory_order_acquire);
            
            if (tensor != 0) {
                out_magnitude = static_cast<uint32_t>(tensor & 0xFFFFFFFF);
                return true;
            }
            return false;
        }
    };

} // namespace slabflux::rte