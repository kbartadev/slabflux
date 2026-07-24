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
 * ============================================================================* SlabFlux Context Vault Module
 *
 * Task:
 *  - REGISTER_CONTEXT(Event, Ctx...) -> context_association<Event>::context_type = typelist<Ctx...>
 *  - multiple contexts / events, multiple events / contexts allowed
 *  - compile-time layout, cache-aligned vault
 *  - orphan event (no context) -> has_context = false
 */

#pragma once

#include <type_traits>
#include <tuple>
#include <cstddef>
#include <utility>
#include "slabflux/pipeline/typelist_algebra.hpp"

namespace slabflux {
    struct DummyContext {};
}

namespace slabflux::reflection {
    template <typename T>
    struct context_association {
        using context_type = ::slabflux::core::typelist<>;
        static constexpr bool has_context = false;
    };
}

namespace slabflux::core {

    // -----------------------------------------------------------------------------
    // 2. context_association default + REGISTER_CONTEXT
    // -----------------------------------------------------------------------------

    template <typename Event>
    struct context_association {
        using context_type = typename ::slabflux::reflection::context_association<Event>::context_type;
        static constexpr bool has_context = ::slabflux::reflection::context_association<Event>::has_context;
    };

    #define REGISTER_CONTEXT(EventType, ...)                                      \
    namespace slabflux::core {                                                    \
        template <>                                                               \
        struct context_association<EventType> {                                   \
            using context_type = ::slabflux::core::typelist<__VA_ARGS__>;         \
            static constexpr bool has_context = true;                             \
        };                                                                        \
    }

    // Has-context helper
    template <typename Event>
    inline constexpr bool has_context_v = context_association<Event>::has_context;

    // -----------------------------------------------------------------------------
    // 3. Context Vault – compile-time layout, cache-aligned tároló
    // -----------------------------------------------------------------------------

    // The vault contains a context-typelist belonging to a specific EventDAG.
    // Example: EventDAG = typelist<E1, E2, E3>
    //        ContextList = concat(context_association<E1>::context_type, ...)
    // Simplified here: the user provides the ContextList.

    template <typename... Ctxs>
    struct context_vault {
        // Cache-aligned, non-erased storage
        alignas(64) std::tuple<Ctxs...> storage_;

        constexpr context_vault() noexcept = default;

        template <typename Ctx>
        requires (std::is_same_v<Ctx, Ctxs> || ...)
        Ctx& get() noexcept {
            return std::get<Ctx>(storage_);
        }

        template <typename Ctx>
        requires (std::is_same_v<Ctx, Ctxs> || ...)
        const Ctx& get() const noexcept {
            return std::get<Ctx>(storage_);
        }

        constexpr void clear_all() noexcept {
            storage_ = std::tuple<Ctxs...>{};
        }

        template <typename TargetCtx, std::size_t... Is>
        TargetCtx& get_derived_impl(std::index_sequence<Is...>) noexcept {
            TargetCtx* ptr = nullptr;
            (void)( [&]() -> bool {
                using TType = list_at_t<Is, typelist<Ctxs...>>;
                if constexpr (std::is_base_of_v<TargetCtx, TType> || std::is_same_v<TargetCtx, TType>) {
                    ptr = static_cast<TargetCtx*>(&std::get<Is>(storage_));
                    return true;
                }
                return false;
            }() || ... );
            return *ptr;
        }

        template <typename TargetCtx>
        requires ((std::is_base_of_v<TargetCtx, Ctxs> || std::is_same_v<TargetCtx, Ctxs>) || ...)
        TargetCtx& get_derived() noexcept {
            return get_derived_impl<TargetCtx>(std::index_sequence_for<Ctxs...>{});
        }
    };

    // -----------------------------------------------------------------------------
    // 4. Context list deriválása EventDAG-ból (egyszerűsített modell)
    // -----------------------------------------------------------------------------

    template <typename List>
    using unique_t = typename unique<List>::type;

    // EventDAG → ContextList
    template <typename EventDag>
    struct derive_context_list;

    template <typename... Es>
    struct derive_context_list<typelist<Es...>> {
    private:
        using raw = concat_t<typename context_association<Es>::context_type...>;
    public:
        using type = unique_t<raw>;
    };

    template <typename EventDag>
    using derive_context_list_t = typename derive_context_list<EventDag>::type;

    // -----------------------------------------------------------------------------
    // 5. get_context hook – dispatcher unroller innen hívja
    // -----------------------------------------------------------------------------

    // General form: EventBase, Ctx, Vault
    template <typename EventBase, typename Ctx, typename Vault>
    Ctx& get_context_from_vault(Vault& vault) noexcept {
        // According to the specification:
        // 1. vault.get<Ctx>()
        // 2. static_cast<Ctx&>(vault) – not used here for now
        // 3. thread-local fallback – not implemented here
        // 4. no context – if signature allows
        return vault.template get<Ctx>();
    }

} // namespace slabflux::core

namespace slabflux {
    using core::context_vault;
}
