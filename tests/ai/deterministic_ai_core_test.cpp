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
/* tests/ai/deterministic_ai_core_test.cpp */
#include <gtest/gtest.h>
#include <type_traits>
#include <span>
#include <numeric>
#include <cmath>
#include <type_traits> // For std::is_standard_layout_v
#include "slabflux/ai/deterministic_ai_core.hpp"
#include "slabflux/ai/cognitive_stimulus.hpp"

using namespace slabflux::ai;

// ============================================================================
// 1. HARDWARE LAYOUT & COMPILE-TIME TRAIT SANITIZATION
// ============================================================================

TEST(DeterministicAiCoreTest, MemoryLayoutAndPhysicalInvariants) {
    using default_shape = tensor_shape<32, 8>;
    using core_t = deterministic_ai_core<default_shape>;

    // Enforces that the structure maps to flat binary sequences with no meta overhead
    static_assert(std::is_standard_layout_v<core_t>, "AI Core must maintain standard layout guarantees!");

    core_t core;

    // Physical Determinism: Critical alignment verification to avoid unaligned AVX instruction faults
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&core.memory_state) % 64, 0)
    << "State vector failed 64-byte L1 cache-line alignment!";
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&core.weight_matrix) % 64, 0)
    << "Weight matrix failed 64-byte L1 cache-line alignment!";
    EXPECT_EQ(core.view_state().size(), 256);
}

// ============================================================================
// 2. DYNAMIC RUNTIME PASS & ALGORITHMIC FREEDOM VERIFICATION
// ============================================================================

TEST(DeterministicAiCoreTest, StreamEngineAlgorithmicFlexibility) {
    using shape_t = tensor_shape<2, 8>; // 16 elements
    deterministic_ai_core<shape_t> core;

    // Manually prime state using the explicit zero-copy span interface
    auto state_view = core.view_state();
    float* mutable_state = const_cast<float*>(state_view.data());
    std::fill_n(mutable_state, core.CAPACITY, 10.0f);

    // Verify raw weights loading capability
    alignas(64) float new_weights[16];
    std::fill_n(new_weights, 16, 2.0f);

    // Fixed: Explicit instantiation of fixed-extent span clears the explicit constructor guard
    core.load_weights(std::span<const float, 16>(new_weights, 16));

    // Run a custom, runtime-injected pass (Stochastic Gradient Ascent style modification)
    constexpr float learning_rate = 0.5f;
    core.compute_stream([&](__m256 m_tau, __m256 w_tau) noexcept {
        __m256 v_lr = _mm256_set1_ps(learning_rate);
        return _mm256_fmadd_ps(w_tau, v_lr, m_tau);
    });

    // Mathematical verification: 10.0f + (2.0f * 0.5f) = 11.0f
    auto updated_state = core.view_state();
    for (float val : updated_state) {
        EXPECT_FLOAT_EQ(val, 11.0f);
    }
}

// ============================================================================
// 3. HOT-PATH INTEGRITY & ORIGINAL COMPLIANCE REGRESSION
// ============================================================================

TEST(DeterministicAiCoreTest, NativePipelinePulseRegression) {
    using default_shape = tensor_shape<32, 8>;
    deterministic_ai_core<default_shape> core(0.2f); // eta = 0.2

    // Explicitly zero-initialize state to prevent uninitialized memory interference
    float* mutable_state = const_cast<float*>(core.view_state().data());
    std::fill_n(mutable_state, core.CAPACITY, 0.0f);

    // Set custom weights dynamically
    alignas(64) float temp_weights[core.CAPACITY];
    std::fill_n(temp_weights, core.CAPACITY, 1.5f);

    // Fixed: Explicit instantiation of fixed-extent span
    core.load_weights(std::span<const float, core.CAPACITY>(temp_weights, core.CAPACITY));

    // Mock an incoming pipeline stimulus event
    cognitive_stimulus ev(1000u, 0.4f); // raw_token = 1000, confidence = 0.4

    // Fire event down the hot path dispatch
    core.on(ev);

    // Verify mathematical identity under original instruction variables (diff, update, decay)
    // The raw_token presence implicitly acts as a 1.0f signal activation.
    // e_val = 1.0 * 1.5 = 1.5
    // diff = 1.5 - 0.0 = 1.5
    // update = 0.4 * 1.5 = 0.6
    // decay = 0.2 * 0.0 = 0.0
    // Expected: 0.0 + 0.6 - 0.0 = 0.6f
    auto final_state = core.view_state();
    for (size_t i = 0; i < core.CAPACITY; ++i) {
        EXPECT_NEAR(final_state[i], 0.6f, 1e-4f);
    }
}

// ============================================================================
// 4. FLOATING-POINT STABILITY & ACCUMULATION STRESS TEST
// ============================================================================

TEST(DeterministicAiCoreTest, HighConcentrationPulseStability) {
    using default_shape = tensor_shape<32, 8>;
    deterministic_ai_core<default_shape> core(0.01f); // Microscopic decay to test limits

    // Initialize state to 0 and weights to 1.0f to ensure deterministic mathematical progression
    float* mutable_state = const_cast<float*>(core.view_state().data());
    std::fill_n(mutable_state, core.CAPACITY, 0.0f);

    alignas(64) float temp_weights[core.CAPACITY];
    std::fill_n(temp_weights, core.CAPACITY, 1.0f);
    core.load_weights(std::span<const float, core.CAPACITY>(temp_weights, core.CAPACITY));

    cognitive_stimulus impulse(500u, 0.1f);

    // Stress loop: Fire 10,000 continuous pulses down the engine core
    // Ensures that the loop optimization doesn't introduce instruction reordering drift
    for (int i = 0; i < 10000; ++i) {
        core.on(impulse);
    }

    auto saturated_state = core.view_state();
    for (size_t i = 0; i < core.CAPACITY; ++i) {
        EXPECT_TRUE(std::isfinite(saturated_state[i]));
        EXPECT_GT(saturated_state[i], 0.0f);
    }
}
