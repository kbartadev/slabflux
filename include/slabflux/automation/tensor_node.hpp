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
#include <span>
#include <concepts>
#include <tuple>
#include "slabflux/compute/engine.hpp"
#include "slabflux/meta.hpp"
#include "slabflux/core/managed_data.hpp"
#include "slabflux/automation/tensor_binding.hpp"

namespace slabflux::automation {

    /**
     * @brief Non-intrusive automation wrapper.
     * @details Automatically lifts structural configuration arguments into 
     * type-deduced conceptual structures. Integrates natively with the 
     * core::pipeline via the 'on' handler contract.
     * 
     * @tparam KernelMathPolicy The AVX-accelerated mathematical kernel.
     * @tparam FixedMatrixDimension Total elements in the SIMD state block.
     * @tparam InputEvent The specific event type that triggers a pulse.
     */
    template <typename KernelMathPolicy, size_t FixedMatrixDimension, typename InputEvent>
    requires Tensor<InputEvent>
    class tensor_node {
        // Structural layout is cleanly encapsulated within the type system
        slabflux::compute::matrix<float, FixedMatrixDimension> internal_matrix;
        slabflux::compute::engine<KernelMathPolicy> physical_engine;

    public:
        tensor_node() = default;

        /**
         * @brief Vectorized Pipeline Ingestion.
         * @details Maximizes I-Cache residency by processing a burst of events
         * in a single execution pass.
         */
        template <typename E>
        requires std::is_same_v<E, InputEvent>
        SLAB_HOT void on_vector_batch(const E** events, size_t count) noexcept {
            for (size_t i = 0; i < count; ++i) {
                pulse(*events[i]);
            }
        }

        /**
         * @brief Managed Lifecycle Entry.
         * @details Automatically reclaims the event envelope to the Pool
         * after the mathematical pulse is retired.
         */
        template <typename Pool>
        SLAB_HOT void on(core::managed_data<InputEvent, Pool>& managed_ev) noexcept {
            pulse(*managed_ev);
            managed_ev.release(); // Return to pool
        }

        /**
         * @brief Pipeline Entry Point.
         * @details Maps the incoming event directly to a hardware pulse.
         */
        template <typename E>
        requires std::is_same_v<E, InputEvent>
        SLAB_HOT void on(const E& ev) noexcept {
            pulse(ev);
        }

        /**
         * @brief Manual Pulse Interface.
         * @details Executes the mathematical kernel across the entire matrix 
         * using the deduced SIMD step size (AVX2/AVX-512).
         */
        template <Tensor InputContext>
        SLAB_FORCE_INLINE void pulse(const InputContext& ctx) noexcept {
            // Bridging your tensor contract to the hardware-accelerated physical engine.
            const float* raw_ptr = static_cast<const float*>(ctx.data());
            physical_engine.execute_pulse(internal_matrix, raw_ptr, ctx.numel());
        }

        /**
         * @brief Zero-copy State Observation.
         * @return A read-only span over the internal memory matrix.
         */
        [[nodiscard]] SLAB_FORCE_INLINE std::span<const float, FixedMatrixDimension> view_state() const noexcept {
            return std::span<const float, FixedMatrixDimension>(internal_matrix.storage, FixedMatrixDimension);
        }

        /**
         * @brief Static Topology Discovery.
         * @details Informs the core::demuxer which events this node is eligible to handle.
         */
        using event_types = std::tuple<InputEvent>;

        /**
         * @brief Direct read access for external telemetry.
         * @deprecated Use view_state() for safer boundary-checked access.
         */
        [[nodiscard]] SLAB_FORCE_INLINE const float* peek_raw_buffer() const noexcept {
            return internal_matrix.storage;
        }
    };

} // namespace slabflux::automation
