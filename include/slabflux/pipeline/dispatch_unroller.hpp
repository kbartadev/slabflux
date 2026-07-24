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
#include <utility>
#include <type_traits>

#include "slabflux/pipeline/context_vault.hpp"

namespace slabflux::core {

    // -------------------------------------------------------------------------
    // 0. Type Extraction Helpers
    // -------------------------------------------------------------------------
    template <typename T, typename = void>
    struct unwrap_event_type { using type = T; };

    template <typename T>
    struct unwrap_event_type<T, std::void_t<decltype(*std::declval<T&>())>> {
        using type = std::remove_cv_t<std::remove_reference_t<decltype(*std::declval<T&>())>>;
    };

    // -------------------------------------------------------------------------
    // 0. Ephemeral Vault Generator
    // -------------------------------------------------------------------------
    template <typename List>
    struct vault_maker;

    template <typename... Ctxs>
    struct vault_maker<typelist<Ctxs...>> {
        using type = context_vault<Ctxs...>;
    };

    template <typename List>
    using make_vault_t = typename vault_maker<List>::type;

    template <typename List>
    struct expand_typelist_ancestors;

    template <typename... Ts>
    struct expand_typelist_ancestors<typelist<Ts...>> {
        using type = unique_t<concat_t<ancestors_t<Ts>...>>;
    };

    template <typename List>
    using expand_typelist_ancestors_t = typename expand_typelist_ancestors<List>::type;

    template <typename Vault, typename Ctx>
    requires (requires(Vault& v) { v.template get<Ctx>(); } || requires(Vault& v) { v.template get_derived<Ctx>(); } || requires(Vault& v) { static_cast<Ctx&>(v); })
    SLAB_FORCE_INLINE decltype(auto) extract_ctx(Vault& vault) {
        if constexpr (requires { vault.template get<Ctx>(); }) {
            return vault.template get<Ctx>();
        } else if constexpr (requires { vault.template get_derived<Ctx>(); }) {
            return vault.template get_derived<Ctx>();
        } else {
            // Gracefully handles raw struct injection during unit testing
            return static_cast<Ctx&>(vault);
        }
    }

    // -------------------------------------------------------------------------
    // 1.5 Handler Compatibility Trait (SFINAE Shield)
    // -------------------------------------------------------------------------
    template <typename H, typename EBase, typename VaultInstance, typename CtxList>
    struct is_handler_compatible;

    template <typename H, typename EBase, typename VaultInstance, typename... Ctxs>
    struct is_handler_compatible<H, EBase, VaultInstance, typelist<Ctxs...>> {
        static constexpr bool has_ctx_match = 
            std::is_same_v<VaultInstance, std::nullptr_t> ? false :
            (... || (requires(VaultInstance& v) { extract_ctx<VaultInstance, Ctxs>(v); } &&
               (has_on_ref_ctx_bool<H, EBase, Ctxs>::value || has_on_cref_ctx_bool<H, EBase, Ctxs>::value ||
                has_on_ref_ctx_void<H, EBase, Ctxs>::value || has_on_cref_ctx_void<H, EBase, Ctxs>::value ||
                has_on_ptr_bool<H, EBase, Ctxs>::value || has_on_cptr_bool<H, EBase, Ctxs>::value ||
                has_on_ptr_void<H, EBase, Ctxs>::value || has_on_cptr_void<H, EBase, Ctxs>::value)));

        static constexpr bool has_free_match =
            has_on_ref_bool<H, EBase, void>::value || has_on_cref_bool<H, EBase, void>::value ||
            has_on_ref_void<H, EBase, void>::value || has_on_cref_void<H, EBase, void>::value ||
            has_on_ptr_bool<H, EBase, void>::value || has_on_cptr_bool<H, EBase, void>::value ||
            has_on_ptr_void<H, EBase, void>::value || has_on_cptr_void<H, EBase, void>::value;

        static constexpr bool value = has_ctx_match || has_free_match;
    };

    template <std::size_t IE, std::size_t NE, typename H, typename EventDag, typename AllContexts, typename VaultInstance, typename EventInstance, typename HandlerInstance>
    SLAB_FORCE_INLINE void unroll_event_dag(HandlerInstance& h, EventInstance& ev, VaultInstance& vault, bool& halted) noexcept {
        if constexpr (IE < NE) {
            using EBase = list_at_t<IE, EventDag>;
            using ExpandedContexts = expand_typelist_ancestors_t<AllContexts>;
            
            if constexpr (is_handler_compatible<H, EBase, VaultInstance, ExpandedContexts>::value) {
                try_invoke_exact<H, EBase>(h, ev, vault, ExpandedContexts{}, halted);
            }
            unroll_event_dag<IE + 1, NE, H, EventDag, AllContexts, VaultInstance, EventInstance, HandlerInstance>(h, ev, vault, halted);
        }
    }

    // -------------------------------------------------------------------------
    // 1. C++20 Concept-Driven Signature Invoker
    // -------------------------------------------------------------------------
    template <typename H, typename EBase, typename HandlerInstance, typename EventInstance, typename VaultInstance, typename... Ctxs>
    SLAB_FORCE_INLINE bool try_invoke_exact(HandlerInstance& h, EventInstance& ev, VaultInstance& vault, typelist<Ctxs...>, bool& halted) noexcept {
        bool matched = false;

        // Fold expression: Execute for ALL valid contexts that match the exact signature for EBase
        (void)( [&]<typename Ctx>() -> bool {
            if (halted) return true; // Short-circuit only if the pipeline is explicitly halted
            if constexpr (!std::is_same_v<VaultInstance, std::nullptr_t>) {
                if constexpr (requires { extract_ctx<VaultInstance, Ctx>(vault); }) {
                    auto& ctx = extract_ctx<VaultInstance, Ctx>(vault);

                    if constexpr (has_on_ref_ctx_bool<H, EBase, Ctx>::value || has_on_cref_ctx_bool<H, EBase, Ctx>::value ||
                                  has_on_ptr_bool<H, EBase, Ctx>::value || has_on_cptr_bool<H, EBase, Ctx>::value) {
                        matched = true;
                        if (signature_invoke<H, EBase, EventInstance, Ctx&>(h, ev, ctx)) {
                            halted = true;
                        }
                    } else if constexpr (has_on_ref_ctx_void<H, EBase, Ctx>::value || has_on_cref_ctx_void<H, EBase, Ctx>::value ||
                                         has_on_ptr_void<H, EBase, Ctx>::value || has_on_cptr_void<H, EBase, Ctx>::value) {
                        matched = true;
                        signature_invoke<H, EBase, EventInstance, Ctx&>(h, ev, ctx);
                    }
                }
            }
            return false;
        }.template operator()<Ctxs>() || ... );

        if (matched) return true;

        // Fallback: Try context-free signatures (Orphans / Ignored Contexts)
        if constexpr (has_on_ptr_bool<H, EBase, void>::value || has_on_cptr_bool<H, EBase, void>::value) {
            matched = true;
            if (signature_invoke<H, EBase, EventInstance, std::nullptr_t>(h, ev, nullptr)) halted = true;
        } else if constexpr (has_on_ptr_void<H, EBase, void>::value || has_on_cptr_void<H, EBase, void>::value) {
            matched = true;
            signature_invoke<H, EBase, EventInstance, std::nullptr_t>(h, ev, nullptr);
        } else if constexpr (has_on_ref_bool<H, EBase, void>::value || has_on_cref_bool<H, EBase, void>::value) {
            matched = true;
            if (signature_invoke<H, EBase, EventInstance, std::nullptr_t>(h, ev, nullptr)) halted = true;
        } else if constexpr (has_on_ref_void<H, EBase, void>::value || has_on_cref_void<H, EBase, void>::value) {
            matched = true;
            signature_invoke<H, EBase, EventInstance, std::nullptr_t>(h, ev, nullptr);
        }
        return matched;
    }

    // -------------------------------------------------------------------------
    // 2. Full Top-Down Topological Unrolling
    // -------------------------------------------------------------------------
    template <typename HandlerList, typename EventDag, typename AllContexts, typename Phase, typename PipelineStorage, typename EventInstance, typename VaultInstance>
    SLAB_FORCE_INLINE void unroll_pipeline(PipelineStorage& pipe, EventInstance& ev, VaultInstance& vault) noexcept {
        constexpr std::size_t NH = list_size<HandlerList>::value;
        constexpr std::size_t NE = list_size<EventDag>::value;

        bool halted = false;

        [&]<std::size_t... IH>(std::index_sequence<IH...>) {
            ( [&]{
                if (halted) return;

                using H = list_at_t<IH, HandlerList>;
                if constexpr (phase_filter<H>::template accept<Phase>()) {
                    auto& h = pipe.template get<H>();

                    // Compile-time unrolling over Event DAG to find the exact most-derived match,
                    // short-circuiting instantiation to shield unconstrained templates.
                    unroll_event_dag<0, NE, H, EventDag, AllContexts, VaultInstance, EventInstance, decltype(h)>(h, ev, vault, halted);
                }
            }(), ... );
        }(std::make_index_sequence<NH>{});
    }

    // -------------------------------------------------------------------------
    // 3. pipeline<Handlers...>::dispatch
    // -------------------------------------------------------------------------
    template <typename... Handlers>
    class alignas(sizeof(void*)) pipeline {
        std::tuple<Handlers...> handlers_;

        // Helper to extract the correct underlying handler instance dynamically
        template <typename H, typename... Ts, std::size_t... Is>
        SLAB_FORCE_INLINE H& get_handler_impl(std::tuple<Ts...>& t, std::index_sequence<Is...>) noexcept {
            H* ptr = nullptr;
            (void)( [&]() -> bool {
                if constexpr (std::is_base_of_v<H, std::remove_reference_t<Ts>> || std::is_same_v<H, std::remove_reference_t<Ts>>) {
                    ptr = static_cast<H*>(&std::get<Is>(t));
                    return true;
                }
                return false;
            }() || ... );
            return *ptr;
        }

        template <typename Phase, typename Vault, typename Event>
        SLAB_FORCE_INLINE void dispatch_impl(Vault& vault, Event&& ev) noexcept {
            // Full sorted DAG of handlers evaluated dynamically to allow MOC traits at end of TU
            using handler_list = topo_sort_t<unique_t<concat_t<handler_dag_t<std::remove_cv_t<std::remove_reference_t<Handlers>>>...>>>;

            using E = std::remove_cv_t<std::remove_reference_t<Event>>;
            if constexpr (std::is_pointer_v<E>) {
                using BaseE = std::remove_cv_t<std::remove_pointer_t<E>>;
                using E_DAG = event_dag_t<BaseE>;
                using AllContexts = derive_context_list_t<E_DAG>;
                unroll_pipeline<handler_list, E_DAG, AllContexts, Phase>(*this, *ev, vault);
            } else if constexpr (requires { *ev; }) {
                // Smart pointer check
                using BaseE = std::remove_cv_t<std::remove_reference_t<decltype(*ev)>>;
                using E_DAG = event_dag_t<BaseE>;
                using AllContexts = derive_context_list_t<E_DAG>;
                unroll_pipeline<handler_list, E_DAG, AllContexts, Phase>(*this, *ev, vault);
            } else {
                using E_DAG = event_dag_t<E>;
                using AllContexts = derive_context_list_t<E_DAG>;
                unroll_pipeline<handler_list, E_DAG, AllContexts, Phase>(*this, ev, vault);
            }
        }

    public:
        explicit constexpr pipeline(Handlers... hs) noexcept
        : handlers_(std::forward<Handlers>(hs)...) {}

        template <typename H>
        SLAB_FORCE_INLINE H& get() noexcept {
            return get_handler_impl<H>(handlers_, std::index_sequence_for<Handlers...>{});
        }

        // 1. dispatch(event)
        template <typename Event>
        SLAB_HOT void dispatch(Event&& ev) noexcept {
            using E = std::remove_cv_t<std::remove_reference_t<Event>>;
            using BaseE = typename unwrap_event_type<E>::type;
            using E_DAG = event_dag_t<BaseE>;
            using AllContexts = derive_context_list_t<E_DAG>;
            
            if constexpr (list_size<AllContexts>::value > 0) {
                make_vault_t<AllContexts> ephemeral_vault;
                dispatch_impl<default_phase>(ephemeral_vault, std::forward<Event>(ev));
            } else {
                std::nullptr_t null_vault = nullptr;
                dispatch_impl<default_phase>(null_vault, std::forward<Event>(ev));
            }
        }

        // 2. dispatch(vault, event)
        template <typename Vault, typename Event>
        SLAB_HOT void dispatch(Vault& vault, Event&& ev) noexcept {
            dispatch_impl<default_phase>(vault, std::forward<Event>(ev));
        }
    };

    // Explicit CTAD deduction guide to force lvalue reference storage
    template<typename... Handlers>
    pipeline(Handlers&...) -> pipeline<Handlers&...>;

} // namespace slabflux
