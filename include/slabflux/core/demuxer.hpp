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
#include "slabflux/core/memory.hpp"

namespace slabflux::core {
    /**
     * @brief Concept for events that can be dispatched via the Demuxer.
     * @details Requires a static uint16_t ID field for hardware-level tag resolution.
     */
    template <typename T>
    concept DemuxableEvent = requires {
        { T::ID } -> std::convertible_to<uint16_t>;
    };

    /**
     * @brief Static Demuxer.
     * @details Re-architected using C++20 fold expressions and requirements for O(1) 
     * branchless dispatch. Prevents pipeline stalls by favoring CMOV/Jump Tables over binary searches.
     * @tparam SupportedEvents Variadic list of event types managed by this demuxer.
     */
    template <DemuxableEvent... SupportedEvents>
    struct demuxer {

        /**
         * @brief Routes a tagged transport token to its appropriate pipeline handler.
         * @tparam PipelineType The target logic engine satisfying the dispatch() contract.
         */
        template <typename PipelineType>
        SLAB_FORCE_INLINE static void route(tagged_pointer tp, PipelineType& pipe) noexcept {
            const uint16_t current_tag = tp.tag();
            void* const payload = tp.ptr();

            // C++20 Validation: Ensures the target pipeline has a matching dispatch() for all registered events.
            static_assert((requires(PipelineType& p, SupportedEvents& e) { p.dispatch(e); } && ...),
                          "SLABFLUX: Pipeline must implement dispatch(T&) for all registered SupportedEvents.");

            // Non-short-circuiting Fold Expression: Eliminates conditional jumps in the hot path.
            // Ternary logic allows the compiler to utilize predicative execution (e.g., AVX masks or CMOV).
            [[maybe_unused]] bool found = false;
            ((current_tag == SupportedEvents::ID ? (
                pipe.dispatch(*reinterpret_cast<SupportedEvents*>(payload)), (void)(found = true)
            ) : void()), ...);

#ifndef NDEBUG
            if (SL_UNLIKELY(!found)) {
                std::cerr << "[ERR] Unknown tag in demuxer: " << current_tag << "\n";
            }
#endif
        }

        template <typename PipelineType>
        struct instance {
            PipelineType& pipe;
            explicit instance(PipelineType& p) : pipe(p) {}

            SLAB_FORCE_INLINE void dispatch(tagged_pointer tp) noexcept {
                demuxer<SupportedEvents...>::route(tp, pipe);
            }

            SLAB_FORCE_INLINE void dispatch_simd(__m256i vec) noexcept {
                alignas(32) tagged_pointer tps[4];
                _mm256_store_si256(reinterpret_cast<__m256i*>(tps), vec);
                
                #pragma GCC unroll 4
                for (int i = 0; i < 4; ++i) {
                    dispatch(tps[i]);
                }
            }
        };
    };
}
