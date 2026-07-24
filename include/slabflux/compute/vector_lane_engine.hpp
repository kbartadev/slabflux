/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#pragma once

#include <immintrin.h>
#include <cstdint>
#include <cstddef>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/compute/kernels.hpp"
#include "slabflux/compute/simd_invariant_guard.hpp"

namespace slabflux::compute {

    /*
     * INVARIANTS ENFORCED:
     * - Compile-time: DIW-E Register Pressure & Saturation Envelope.
     * - Compile-time: T satisfies SimdCompatibleData concept.
     * - Compile-time: Absolute cache-line tiling for the engine class.
     * - Runtime Numeric: NaN and Infinity detection post-ALU 
     *   (active if SLABFLUX_SIMD_SANITIZE_NUMERICS is defined).
     * - ILP Strategy: Deterministic interleaved chains with zero 
     *   intra-block data dependencies.
     */

    /**
     * @brief Vector Lane Engine.
     * @details Data parallel processor mapping sequential algorithms to 
     * hardware lane architectures. Fuses directly with metaprogrammed 
     * `execution_graph` sequences to process massive homogenous datasets 
     * with absolute zero branching and zero L1 cache misses.
     */
    template <typename KernelGraph, typename T, size_t Capacity, bool StrictDIWE = true>
    class alignas(64) vector_lane_engine {
        static_assert(Capacity > 0, "Capacity must be non-zero");
        static_assert(SimdCompatibleData<T>, "T must be a primitive arithmetic type eligible for SIMD vectorization");
        
        // Prevent false sharing or vector spillage by enforcing precise matrix bounds
        static_assert((sizeof(T) * Capacity) % 64 == 0, "Array size must exactly tile across cache lines.");

        // Cache-aligned matrices matching 64-byte structural boundaries
        alignas(64) T memory_state_[Capacity]{0};
        alignas(64) T input_stream_[Capacity]{0};
        alignas(64) T parameters_[Capacity]{0};

    public:
        constexpr vector_lane_engine() noexcept = default;

        [[nodiscard]] SLAB_FORCE_INLINE T* state() noexcept { return memory_state_; }
        [[nodiscard]] SLAB_FORCE_INLINE const T* state() const noexcept { return memory_state_; }

        [[nodiscard]] SLAB_FORCE_INLINE T* inputs() noexcept { return input_stream_; }
        [[nodiscard]] SLAB_FORCE_INLINE T* parameters() noexcept { return parameters_; }
        
        // Preserved for API parity; completely optimized out via DIW-E
        SLAB_FORCE_INLINE void set_expected_identity(uint32_t) noexcept {}

        /**
         * @brief Executes the statically defined KernelGraph across all data lanes.
         */
        SLAB_HOT void execute() noexcept {
#if defined(__AVX512F__)
            if constexpr (std::is_same_v<T, float>) {
                if constexpr (StrictDIWE) {
                    // DIW-E Adaptive Unrolling: 
                    // Determines the absolute maximum micro-op interleaving depth 
                    // possible without spilling physical ZMM registers.
                    if constexpr (DIWEnforceable<Capacity, 16, 4, 32>) {
                        for (size_t i = 0; i < Capacity; i += 64) {
                            // Cycle 1: Memory Fetch Block
                            __m512 v_m0 = _mm512_load_ps(&memory_state_[i]);
                            __m512 v_i0 = _mm512_load_ps(&input_stream_[i]);
                            __m512 v_p0 = _mm512_load_ps(&parameters_[i]);

                            __m512 v_m1 = _mm512_load_ps(&memory_state_[i + 16]);
                            __m512 v_i1 = _mm512_load_ps(&input_stream_[i + 16]);
                            __m512 v_p1 = _mm512_load_ps(&parameters_[i + 16]);

                            __m512 v_m2 = _mm512_load_ps(&memory_state_[i + 32]);
                            __m512 v_i2 = _mm512_load_ps(&input_stream_[i + 32]);
                            __m512 v_p2 = _mm512_load_ps(&parameters_[i + 32]);

                            __m512 v_m3 = _mm512_load_ps(&memory_state_[i + 48]);
                            __m512 v_i3 = _mm512_load_ps(&input_stream_[i + 48]);
                            __m512 v_p3 = _mm512_load_ps(&parameters_[i + 48]);

                            // Cycle 2: Pipelined Execution Block (No internal dependencies)
                            __m512 v_res0 = KernelGraph::template compute<__m512>(v_m0, v_i0, v_p0);
                            __m512 v_res1 = KernelGraph::template compute<__m512>(v_m1, v_i1, v_p1);
                            __m512 v_res2 = KernelGraph::template compute<__m512>(v_m2, v_i2, v_p2);
                            __m512 v_res3 = KernelGraph::template compute<__m512>(v_m3, v_i3, v_p3);

                            // Cycle 3: Cold-Path Gated Integrity Sweep
                            SLAB_SIMD_SANITIZE_FLOATS_512(v_res0);
                            SLAB_SIMD_SANITIZE_FLOATS_512(v_res1);
                            SLAB_SIMD_SANITIZE_FLOATS_512(v_res2);
                            SLAB_SIMD_SANITIZE_FLOATS_512(v_res3);

                            // Cycle 4: Flush
                            _mm512_store_ps(&memory_state_[i], v_res0);
                            _mm512_store_ps(&memory_state_[i + 16], v_res1);
                            _mm512_store_ps(&memory_state_[i + 32], v_res2);
                            _mm512_store_ps(&memory_state_[i + 48], v_res3);
                        }
                    } else if constexpr (DIWEnforceable<Capacity, 16, 2, 32>) {
                        for (size_t i = 0; i < Capacity; i += 32) {
                            __m512 v_m0 = _mm512_load_ps(&memory_state_[i]);
                            __m512 v_i0 = _mm512_load_ps(&input_stream_[i]);
                            __m512 v_p0 = _mm512_load_ps(&parameters_[i]);

                            __m512 v_m1 = _mm512_load_ps(&memory_state_[i + 16]);
                            __m512 v_i1 = _mm512_load_ps(&input_stream_[i + 16]);
                            __m512 v_p1 = _mm512_load_ps(&parameters_[i + 16]);

                            __m512 v_res0 = KernelGraph::template compute<__m512>(v_m0, v_i0, v_p0);
                            __m512 v_res1 = KernelGraph::template compute<__m512>(v_m1, v_i1, v_p1);

                            SLAB_SIMD_SANITIZE_FLOATS_512(v_res0);
                            SLAB_SIMD_SANITIZE_FLOATS_512(v_res1);

                            _mm512_store_ps(&memory_state_[i], v_res0);
                            _mm512_store_ps(&memory_state_[i + 16], v_res1);
                        }
                    } else {
                        static_assert(DIWEnforceable<Capacity, 16, 1, 32>, "DIW-E Fatal: Missing 16-lane alignment.");
                        for (size_t i = 0; i < Capacity; i += 16) {
                            __m512 v_m = _mm512_load_ps(&memory_state_[i]);
                            __m512 v_i = _mm512_load_ps(&input_stream_[i]);
                            __m512 v_p = _mm512_load_ps(&parameters_[i]);
                            
                            __m512 v_res = KernelGraph::template compute<__m512>(v_m, v_i, v_p);
                            SLAB_SIMD_SANITIZE_FLOATS_512(v_res);
                            _mm512_store_ps(&memory_state_[i], v_res);
                        }
                    }
                } else {
                    for (size_t i = 0; i < Capacity; i += 16) {
                        __m512 v_m = _mm512_load_ps(&memory_state_[i]);
                        __m512 v_i = _mm512_load_ps(&input_stream_[i]);
                        __m512 v_p = _mm512_load_ps(&parameters_[i]);
                        
                        __m512 v_res = KernelGraph::template compute<__m512>(v_m, v_i, v_p);
                        SLAB_SIMD_SANITIZE_FLOATS_512(v_res);
                        _mm512_store_ps(&memory_state_[i], v_res);
                    }
                }
                return;
            }
#endif

#if defined(__AVX2__)
            if constexpr (std::is_same_v<T, float>) {
                if constexpr (StrictDIWE) {
                    if constexpr (DIWEnforceable<Capacity, 8, 4, 16>) {
                        for (size_t i = 0; i < Capacity; i += 32) {
                            __m256 v_m0 = _mm256_load_ps(&memory_state_[i]);
                            __m256 v_i0 = _mm256_load_ps(&input_stream_[i]);
                            __m256 v_p0 = _mm256_load_ps(&parameters_[i]);

                            __m256 v_m1 = _mm256_load_ps(&memory_state_[i + 8]);
                            __m256 v_i1 = _mm256_load_ps(&input_stream_[i + 8]);
                            __m256 v_p1 = _mm256_load_ps(&parameters_[i + 8]);

                            __m256 v_m2 = _mm256_load_ps(&memory_state_[i + 16]);
                            __m256 v_i2 = _mm256_load_ps(&input_stream_[i + 16]);
                            __m256 v_p2 = _mm256_load_ps(&parameters_[i + 16]);

                            __m256 v_m3 = _mm256_load_ps(&memory_state_[i + 24]);
                            __m256 v_i3 = _mm256_load_ps(&input_stream_[i + 24]);
                            __m256 v_p3 = _mm256_load_ps(&parameters_[i + 24]);

                            __m256 v_res0 = KernelGraph::template compute<__m256>(v_m0, v_i0, v_p0);
                            __m256 v_res1 = KernelGraph::template compute<__m256>(v_m1, v_i1, v_p1);
                            __m256 v_res2 = KernelGraph::template compute<__m256>(v_m2, v_i2, v_p2);
                            __m256 v_res3 = KernelGraph::template compute<__m256>(v_m3, v_i3, v_p3);
                            
                            SLAB_SIMD_SANITIZE_FLOATS_256(v_res0);
                            SLAB_SIMD_SANITIZE_FLOATS_256(v_res1);
                            SLAB_SIMD_SANITIZE_FLOATS_256(v_res2);
                            SLAB_SIMD_SANITIZE_FLOATS_256(v_res3);

                            _mm256_store_ps(&memory_state_[i], v_res0);
                            _mm256_store_ps(&memory_state_[i + 8], v_res1);
                            _mm256_store_ps(&memory_state_[i + 16], v_res2);
                            _mm256_store_ps(&memory_state_[i + 24], v_res3);
                        }
                    } else if constexpr (DIWEnforceable<Capacity, 8, 2, 16>) {
                        for (size_t i = 0; i < Capacity; i += 16) {
                            __m256 v_m0 = _mm256_load_ps(&memory_state_[i]);
                            __m256 v_i0 = _mm256_load_ps(&input_stream_[i]);
                            __m256 v_p0 = _mm256_load_ps(&parameters_[i]);

                            __m256 v_m1 = _mm256_load_ps(&memory_state_[i + 8]);
                            __m256 v_i1 = _mm256_load_ps(&input_stream_[i + 8]);
                            __m256 v_p1 = _mm256_load_ps(&parameters_[i + 8]);

                            __m256 v_res0 = KernelGraph::template compute<__m256>(v_m0, v_i0, v_p0);
                            __m256 v_res1 = KernelGraph::template compute<__m256>(v_m1, v_i1, v_p1);

                            SLAB_SIMD_SANITIZE_FLOATS_256(v_res0);
                            SLAB_SIMD_SANITIZE_FLOATS_256(v_res1);

                            _mm256_store_ps(&memory_state_[i], v_res0);
                            _mm256_store_ps(&memory_state_[i + 8], v_res1);
                        }
                    } else {
                        static_assert(DIWEnforceable<Capacity, 8, 1, 16>, "DIW-E Fatal: Missing 8-lane alignment.");
                        for (size_t i = 0; i < Capacity; i += 8) {
                            __m256 v_m = _mm256_load_ps(&memory_state_[i]);
                            __m256 v_i = _mm256_load_ps(&input_stream_[i]);
                            __m256 v_p = _mm256_load_ps(&parameters_[i]);
                            
                            __m256 v_res = KernelGraph::template compute<__m256>(v_m, v_i, v_p);
                            SLAB_SIMD_SANITIZE_FLOATS_256(v_res);
                            _mm256_store_ps(&memory_state_[i], v_res);
                        }
                    }
                } else {
                    for (size_t i = 0; i < Capacity; i += 8) {
                        __m256 v_m = _mm256_load_ps(&memory_state_[i]);
                        __m256 v_i = _mm256_load_ps(&input_stream_[i]);
                        __m256 v_p = _mm256_load_ps(&parameters_[i]);
                        
                        __m256 v_res = KernelGraph::template compute<__m256>(v_m, v_i, v_p);
                        SLAB_SIMD_SANITIZE_FLOATS_256(v_res);
                        _mm256_store_ps(&memory_state_[i], v_res);
                    }
                }
                return;
            }
#endif

            // Trivial Scalar Fallback
            #pragma GCC unroll 4
            for (size_t i = 0; i < Capacity; ++i) {
                T res = KernelGraph::template compute<T>(
                    memory_state_[i], input_stream_[i], parameters_[i]
                );
                memory_state_[i] = res;
            }
        }

        /**
         * @brief Zeros out the state matrix deterministically.
         * @details Uses non-temporal-style loops to prevent `std::memset` branching.
         */
        SLAB_HOT void reset_state() noexcept {
#if defined(__AVX512F__)
            const __m512i zero = _mm512_setzero_si512();
            for (size_t i = 0; i < Capacity; i += 16) {
                _mm512_store_ps(&memory_state_[i], _mm512_castsi512_ps(zero));
            }
#elif defined(__AVX2__)
            const __m256i zero = _mm256_setzero_si256();
            for (size_t i = 0; i < Capacity; i += 8) {
                _mm256_store_ps(&memory_state_[i], _mm256_castsi256_ps(zero));
            }
#else
            for (size_t i = 0; i < Capacity; ++i) memory_state_[i] = 0;
#endif
        }
    };

} // namespace slabflux::compute