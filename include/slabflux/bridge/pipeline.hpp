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
#include <tuple>
#include "slabflux/core/physics.hpp"
#include "slabflux/core/context.hpp"

namespace slabflux::bridge {

    template <typename... Strategies>
    class pipeline {
        std::tuple<Strategies&...> strategies_;

        // AXIS 3: STRATEGY DEPTH (Z) + CONTEXT PROJECTION (W)
        template <typename CurrentStrategyLayer, typename DataLayer, typename OriginalEvent, typename S, typename Ctx>
        SLAB_FORCE_INLINE void execute_strategy_axis(S& s, core::event_ptr<OriginalEvent>& ev, Ctx& ctx) {

            if constexpr (core::has_base_type_v<CurrentStrategyLayer>) {
                execute_strategy_axis<typename CurrentStrategyLayer::base_type, DataLayer>(
                    static_cast<typename CurrentStrategyLayer::base_type&>(s), ev, ctx);
            }

            if (ev) [[likely]] {
                DataLayer* data_ptr = static_cast<DataLayer*>(ev.get());

                // ====================================================================
                // THE CASCADING SIGNATURE PROBES
                // The compiler natively figures out exactly what the user wrote!
                // ====================================================================

                // Signature 1: Full Arity (Event + Context)
                if constexpr (requires(CurrentStrategyLayer& st, DataLayer& dl, Ctx& cx) { st.on(dl, cx); }) {
                    s.on(*data_ptr, ctx);
                }
                // Signature 2: Event Only (User doesn't care about state)
                else if constexpr (requires(CurrentStrategyLayer& st, DataLayer& dl) { st.on(dl); }) {
                    s.on(*data_ptr);
                }
                // Signature 3: Context Only (User only cares that a specific event occurred)
                else if constexpr (requires(CurrentStrategyLayer& st, Ctx& cx) { st.on(cx); }) {
                    s.on(ctx);
                }
                // Signature 4: Void Trigger (Pure signal, no parameters)
                else if constexpr (requires(CurrentStrategyLayer& st) { st.on(); }) {
                    s.on();
                }
            }
        }

        // AXIS 2: DATA DEPTH (Y)
        template <typename CurrentDataLayer, typename OriginalEvent, typename S, typename Ctx>
        SLAB_FORCE_INLINE void execute_data_axis(S& s, core::event_ptr<OriginalEvent>& ev, Ctx& ctx) {

            if constexpr (core::has_base_type_v<CurrentDataLayer>) {
                execute_data_axis<typename CurrentDataLayer::base_type, OriginalEvent>(s, ev, ctx);
            }

            if (ev) [[likely]] {
                execute_strategy_axis<S, CurrentDataLayer>(s, ev, ctx);
            }
        }

        template <typename OriginalEvent, typename S, typename Ctx>
        SLAB_FORCE_INLINE bool trigger_chip(S& s, core::event_ptr<OriginalEvent>& ev, Ctx& ctx) {
            if (ev) execute_data_axis<OriginalEvent, OriginalEvent>(s, ev, ctx);
            return static_cast<bool>(ev); // False if the memory was stolen by the Bridge
        }

    public:
        explicit pipeline(Strategies&... s) : strategies_(s...) {}

        // AXIS 1: THE PIPELINE SEQUENCE (X)
        template <typename Event, typename ContextType>
        SLAB_FORCE_INLINE void dispatch(core::event_ptr<Event>& ev, ContextType& ctx) {
            if (!ev) [[unlikely]] return;

            ctx._active_smart_ptr = &ev; // Plug the socket in

            // C++17 Variadic Fold Expression -> Unrolls X-Axis
            std::apply([&](auto&... strat) {
                (trigger_chip(strat, ev, ctx) && ...);
            }, strategies_);

            ctx._active_smart_ptr = nullptr; // Unplug the socket safely
            if (!ev) ev.detach(); // Safety net
        }
    };
}
