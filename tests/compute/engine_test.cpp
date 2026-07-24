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
 * ============================================================================* @file engine_test.cpp
 * @brief Unit tests for the Engine.
 * @details Validates the core execution engine's correctness and performance invariants.
 */
 
#include <gtest/gtest.h>
#include <cstring>
#include <type_traits>
#include "slabflux/compute/engine.hpp"
#include "slabflux/compute/kernels.hpp"
#include "slabflux/automation/tensor_node.hpp"

struct alignas(64) mock_stimulus_packet {
    float signal_value;
    float modifier_weight;

    mock_stimulus_packet(float sig, float weight) : signal_value(sig), modifier_weight(weight) {}

    // Structural Honesty: Satisfies the automation::Tensor concept
    [[nodiscard]] const void* data() const noexcept { return &signal_value; }
    [[nodiscard]] size_t numel() const noexcept { return 2; }
    [[nodiscard]] size_t dim() const noexcept { return 1; }
    [[nodiscard]] std::span<const size_t> shape() const noexcept { 
        static constexpr size_t s = 2; return {&s, 1}; 
    }
    [[nodiscard]] slabflux::automation::data_type dtype() const noexcept { 
        return slabflux::automation::data_type::FLOAT32; 
    }
    [[nodiscard]] size_t element_size() const noexcept { return sizeof(float); }
};

// ============================================================================
// BASE DESIGN VERIFICATION (Axioms 11, 12, 16, 21)
// ============================================================================

TEST(SlabfluxComputeCore, StaticLayoutAndPODTraitInvariants) {
    using target_matrix_t = slabflux::compute::matrix<float, 256>;

    // Standard layout confirms flat sequential byte mapping
    static_assert(std::is_standard_layout_v<target_matrix_t>, "Matrix storage tracking must preserve standard binary layout!");

    target_matrix_t matrix;
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&matrix.storage) % 64, 0);
}

TEST(SlabfluxComputeCore, DirectEngineExecutionDeterministicBounds) {
    slabflux::compute::matrix<float, 16> matrix;
    slabflux::compute::engine<slabflux::compute::kernels::rectified_linear_unit> engine;

    // Seed data
    for(size_t i = 0; i < 16; ++i) {
        matrix.storage[i] = -5.0f;
    }

    // Input that triggers ReLU clamp (-5.0f + 2.0f = -3.0f -> clamped to 0.0f)
    mock_stimulus_packet packet{2.0f, 0.0f};
    engine.execute_pulse(matrix, packet);

    for(size_t i = 0; i < 16; ++i) {
        EXPECT_FLOAT_EQ(matrix.storage[i], 0.0f);
    }
}

// ============================================================================
// SUPPLEMENTAL AUTOMATION LAYER VERIFICATION
// ============================================================================

TEST(SlabfluxAutomation, ZeroUserBurdenErgonomicExecution) {
    // The user simply states the high-level intent: Math Policy + Dimension.
    // No pointers, memory ranges, or bitmask alignments are queried on the hot path.
    using automated_layer_t = slabflux::automation::tensor_node<
    slabflux::compute::kernels::fused_exponential_decay, 128, mock_stimulus_packet>;

    automated_layer_t ai_layer{};
    float* mutable_state = const_cast<float*>(ai_layer.peek_raw_buffer());
    std::fill_n(mutable_state, 128, 0.0f); // Explicitly zero-initialize to prevent SIMD garbage
    
    mock_stimulus_packet packet{100.0f, 0.5f};

    // Execute through completely automated interface - zero boilerplate overhead
    ai_layer.pulse(packet);

    const float* result_buffer = ai_layer.peek_raw_buffer();

    // M_new = (0.0f * 0.5f) + (100.0f * 0.5f) = 50.0f
    for(size_t i = 0; i < 128; ++i) {
        EXPECT_NEAR(result_buffer[i], 50.0f, 1e-5f);
    }
}
