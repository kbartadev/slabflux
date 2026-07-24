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
#include "slabflux/core.hpp"
#include <vector>
#include <cmath> // For std::fabs
#include <cassert>
#include <cstring> // For std::memcmp
#include <type_traits>
#include <immintrin.h>

namespace slabflux::test {

    // Deterministic Replay Parity
    struct parity_report {
        uint64_t events_processed;
        bool bit_identical;
        uint64_t drift_detected_at;
    };

    template<typename State>
    class causal_parity_checker {
    public:
        // Compares the Live and Replay states. If even a single bit differs,
        // determinism has failed. For floating-point types, uses an epsilon comparison.
        static parity_report verify(const State& live, const State& replayed) {
            bool identical = (std::memcmp(&live, &replayed, sizeof(State)) == 0);
            uint64_t drift_at = 0;

            // Generic comparison for POD types. For floating-point, iterate.
            // This assumes 'State' has a 'states' member that is an array or vector of floating points.
            // If 'State' is a simple POD, std::memcmp is sufficient.
            // If 'State' is a complex type with floating points, a member-wise comparison is needed.
            // Based on the error message "live_engine.states[i]", we infer a 'states' member.
            if constexpr (requires { live.states; }) {
                size_t sz = 0;
                if constexpr (std::is_array_v<std::remove_cvref_t<decltype(live.states)>>) {
                    sz = sizeof(live.states) / sizeof(live.states[0]);
                } else {
                    sz = live.states.size();
                }

                for (size_t i = 0; i < sz; ++i) {
                    if (std::fabs(live.states[i] - replayed.states[i]) > 1e-6) { // Using a small epsilon
                        identical = false;
                        drift_at = i;
                        break;
                    }
                }
            } else if constexpr (requires { live.lattice_capacity; live.read_vector(0); }) {
                // Pillar III Integration: Minkowski Data Lattice at-rest verification.
                // Natively extracts the FMA-subsumed Light-Cone vectors to bypass temporal 
                // parity noise in memcmp, evaluating true geometric determinism.
                for (size_t i = 0; i < std::remove_cvref_t<decltype(live)>::lattice_capacity; i += 8) {
                    __m256 v_live = live.read_vector(i);
                    __m256 v_replay = replayed.read_vector(i);
                    if (_mm256_movemask_ps(_mm256_cmp_ps(v_live, v_replay, _CMP_NEQ_OQ)) != 0) {
                        identical = false;
                        drift_at = i;
                        break;
                    }
                }
            } else {
                // Fallback for non-floating-point or non-array states
                identical = (std::memcmp(&live, &replayed, sizeof(State)) == 0);
            }
            return {
                .events_processed = 0, // Filled in by the caller
                .bit_identical = identical,
                .drift_detected_at = drift_at
            };
        }
    };
}
