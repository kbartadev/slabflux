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
 * ============================================================================* SlabFlux Signature Router Module
 *
 * Task:
 *  - recognize all supported 'on' signatures:
 *      void / bool
 *      Event& / const Event&
 *      Event* / const Event*
 *  - const-correct projection
 *  - bool → halt, void → always continue (false)
 */

#pragma once

#include <type_traits>
#include <utility>

namespace slabflux::core {

    // -----------------------------------------------------------------------------
    // 1. Base trait: check if 'on' exists at all
    // -----------------------------------------------------------------------------

    template <typename H, typename = void>
    struct has_on : std::false_type { };

    template <typename H>
    struct has_on<H, std::void_t<decltype(&H::on)>> : std::true_type { };

    template <typename H>
    inline constexpr bool has_on_v = has_on<H>::value;

    // -----------------------------------------------------------------------------
    // 2. For a specific (H,E) pair: which form exists?
    // -----------------------------------------------------------------------------

    // void on(E&, Ctx&)
    template <typename H, typename E, typename Ctx, typename = void>
    struct has_on_ref_ctx_void : std::false_type { };

    template <typename H, typename E, typename Ctx>
    struct has_on_ref_ctx_void<H, E, Ctx,
    std::void_t<decltype(static_cast<void(std::remove_reference_t<H>::*)(E&, Ctx&)>(&std::remove_reference_t<H>::on))>> : std::true_type { };

    // bool on(E&, Ctx&)
    template <typename H, typename E, typename Ctx, typename = void>
    struct has_on_ref_ctx_bool : std::false_type { };

    template <typename H, typename E, typename Ctx>
    struct has_on_ref_ctx_bool<H, E, Ctx,
    std::void_t<decltype(static_cast<bool(std::remove_reference_t<H>::*)(E&, Ctx&)>(&std::remove_reference_t<H>::on))>> : std::true_type { };

    // void on(const E&, Ctx&)
    template <typename H, typename E, typename Ctx, typename = void>
    struct has_on_cref_ctx_void : std::false_type { };

    template <typename H, typename E, typename Ctx>
    struct has_on_cref_ctx_void<H, E, Ctx,
    std::void_t<decltype(static_cast<void(std::remove_reference_t<H>::*)(const E&, Ctx&)>(&std::remove_reference_t<H>::on))>> : std::true_type { };

    // bool on(const E&, Ctx&)
    template <typename H, typename E, typename Ctx, typename = void>
    struct has_on_cref_ctx_bool : std::false_type { };

    template <typename H, typename E, typename Ctx>
    struct has_on_cref_ctx_bool<H, E, Ctx,
    std::void_t<decltype(static_cast<bool(std::remove_reference_t<H>::*)(const E&, Ctx&)>(&std::remove_reference_t<H>::on))>> : std::true_type { };

    // -----------------------------------------------------------------------------
    // CONTEXT-FREE HANDLERS
    // -----------------------------------------------------------------------------
    // Uses strict static_cast signature matching to explicitly shield the compiler
    // from instantiating unconstrained template bodies for unmatched base classes.

  template <typename H, typename E, typename Ctx, typename = void>
    struct has_on_ptr_void : std::false_type { };

    template <typename H, typename E, typename Ctx>
    struct has_on_ptr_void<H, E, Ctx,
    std::void_t<decltype(static_cast<void(std::remove_reference_t<H>::*)(E*)>(&std::remove_reference_t<H>::on))>> : std::true_type { };

    template <typename H, typename E, typename Ctx, typename = void>
    struct has_on_ptr_bool : std::false_type { };

    template <typename H, typename E, typename Ctx>
    struct has_on_ptr_bool<H, E, Ctx,
    std::void_t<decltype(static_cast<bool(std::remove_reference_t<H>::*)(E*)>(&std::remove_reference_t<H>::on))>> : std::true_type { };

    template <typename H, typename E, typename Ctx, typename = void>
    struct has_on_cptr_void : std::false_type { };

    template <typename H, typename E, typename Ctx>
    struct has_on_cptr_void<H, E, Ctx,
    std::void_t<decltype(static_cast<void(std::remove_reference_t<H>::*)(const E*)>(&std::remove_reference_t<H>::on))>> : std::true_type { };

    template <typename H, typename E, typename Ctx, typename = void>
    struct has_on_cptr_bool : std::false_type { };

    template <typename H, typename E, typename Ctx>
    struct has_on_cptr_bool<H, E, Ctx,
    std::void_t<decltype(static_cast<bool(std::remove_reference_t<H>::*)(const E*)>(&std::remove_reference_t<H>::on))>> : std::true_type { };

    template <typename H, typename E, typename Ctx, typename = void>
    struct has_on_ref_void : std::false_type { };

    template <typename H, typename E, typename Ctx>
    struct has_on_ref_void<H, E, Ctx,
    std::void_t<decltype(static_cast<void(std::remove_reference_t<H>::*)(E&)>(&std::remove_reference_t<H>::on))>> : std::true_type { };

    template <typename H, typename E, typename Ctx, typename = void>
    struct has_on_ref_bool : std::false_type { };

    template <typename H, typename E, typename Ctx>
    struct has_on_ref_bool<H, E, Ctx,
    std::void_t<decltype(static_cast<bool(std::remove_reference_t<H>::*)(E&)>(&std::remove_reference_t<H>::on))>> : std::true_type { };

    template <typename H, typename E, typename Ctx, typename = void>
    struct has_on_cref_void : std::false_type { };

    template <typename H, typename E, typename Ctx>
    struct has_on_cref_void<H, E, Ctx,
    std::void_t<decltype(static_cast<void(std::remove_reference_t<H>::*)(const E&)>(&std::remove_reference_t<H>::on))>> : std::true_type { };

    template <typename H, typename E, typename Ctx, typename = void>
    struct has_on_cref_bool : std::false_type { };

    template <typename H, typename E, typename Ctx>
    struct has_on_cref_bool<H, E, Ctx,
    std::void_t<decltype(static_cast<bool(std::remove_reference_t<H>::*)(const E&)>(&std::remove_reference_t<H>::on))>> : std::true_type { };

    // -----------------------------------------------------------------------------
    // 3. signature_invoke<H,E> – called by the dispatcher
    // -----------------------------------------------------------------------------

    // ctx_opt: either Ctx& or std::nullptr_t (orphan / context-optional)

    template <typename H, typename E, typename EventInstance, typename CtxOpt>
    SLAB_FORCE_INLINE bool signature_invoke(H& h, EventInstance& ev, CtxOpt ctx_opt) noexcept {
        using Base = E;

        // Const-correct projection
        if constexpr (std::is_const_v<std::remove_reference_t<EventInstance>>) {
            const Base& base = static_cast<const Base&>(ev);

            if constexpr (!std::is_same_v<CtxOpt, std::nullptr_t>) {
                using Ctx = std::remove_reference_t<CtxOpt>;

                if constexpr (has_on_cref_ctx_bool<H, Base, Ctx>::value) {
                    return h.on(base, ctx_opt);
                } else if constexpr (has_on_cref_ctx_void<H, Base, Ctx>::value) {
                    h.on(base, ctx_opt);
                    return false;
                } else if constexpr (has_on_cptr_bool<H, Base, Ctx>::value) {
                    return h.on(&base);
                } else if constexpr (has_on_cptr_void<H, Base, Ctx>::value) {
                    h.on(&base);
                    return false;
                } else if constexpr (has_on_cref_bool<H, Base, Ctx>::value) {
                    return h.on(base);
                } else if constexpr (has_on_cref_void<H, Base, Ctx>::value) {
                    h.on(base);
                    return false;
                } else {
                    // no compatible const-overload -> skip
                    return false;
                }
            } else {
                if constexpr (has_on_cptr_bool<H, Base, void>::value) {
                    return h.on(&base);
                } else if constexpr (has_on_cptr_void<H, Base, void>::value) {
                    h.on(&base);
                    return false;
                } else if constexpr (has_on_cref_bool<H, Base, void>::value) {
                    return h.on(base);
                } else if constexpr (has_on_cref_void<H, Base, void>::value) {
                    h.on(base);
                    return false;
                } else {
                    return false;
                }
            }
        } else {
            Base& base = static_cast<Base&>(ev);

            if constexpr (!std::is_same_v<CtxOpt, std::nullptr_t>) {
                using Ctx = std::remove_reference_t<CtxOpt>;

                if constexpr (has_on_ref_ctx_bool<H, Base, Ctx>::value) {
                    return h.on(base, ctx_opt);
                } else if constexpr (has_on_ref_ctx_void<H, Base, Ctx>::value) {
                    h.on(base, ctx_opt);
                    return false;
                } else if constexpr (has_on_ptr_bool<H, Base, Ctx>::value) {
                    return h.on(&base);
                } else if constexpr (has_on_ptr_void<H, Base, Ctx>::value) {
                    h.on(&base);
                    return false;
                } else if constexpr (has_on_ref_bool<H, Base, Ctx>::value) {
                    return h.on(base);
                } else if constexpr (has_on_ref_void<H, Base, Ctx>::value) {
                    h.on(base);
                    return false;
                } else if constexpr (has_on_cref_ctx_bool<H, Base, Ctx>::value) {
                    return h.on(base, ctx_opt);
                } else if constexpr (has_on_cref_ctx_void<H, Base, Ctx>::value) {
                    h.on(base, ctx_opt);
                    return false;
                } else if constexpr (has_on_cptr_bool<H, Base, Ctx>::value) {
                    return h.on(&base);
                } else if constexpr (has_on_cptr_void<H, Base, Ctx>::value) {
                    h.on(&base);
                    return false;
                } else if constexpr (has_on_cref_bool<H, Base, Ctx>::value) {
                    return h.on(base);
                } else if constexpr (has_on_cref_void<H, Base, Ctx>::value) {
                    h.on(base);
                    return false;
                } else if constexpr (has_on_cref_ctx_bool<H, Base, Ctx>::value) {
                    return h.on(base, ctx_opt);
                } else if constexpr (has_on_cref_ctx_void<H, Base, Ctx>::value) {
                    h.on(base, ctx_opt);
                    return false;
                } else if constexpr (has_on_cptr_bool<H, Base, Ctx>::value) {
                    return h.on(&base);
                } else if constexpr (has_on_cptr_void<H, Base, Ctx>::value) {
                    h.on(&base);
                    return false;
                } else if constexpr (has_on_cref_bool<H, Base, Ctx>::value) {
                    return h.on(base);
                } else if constexpr (has_on_cref_void<H, Base, Ctx>::value) {
                    h.on(base);
                    return false;
                } else {
                    return false;
                }
            } else {
                if constexpr (has_on_ptr_bool<H, Base, void>::value) {
                    return h.on(&base);
                } else if constexpr (has_on_ptr_void<H, Base, void>::value) {
                    h.on(&base);
                    return false;
                } else if constexpr (has_on_ref_bool<H, Base, void>::value) {
                    return h.on(base);
                } else if constexpr (has_on_ref_void<H, Base, void>::value) {
                    h.on(base);
                    return false;
                } else if constexpr (has_on_cptr_bool<H, Base, void>::value) {
                    return h.on(&base);
                } else if constexpr (has_on_cptr_void<H, Base, void>::value) {
                    h.on(&base);
                    return false;
                } else if constexpr (has_on_cref_bool<H, Base, void>::value) {
                    return h.on(base);
                } else if constexpr (has_on_cref_void<H, Base, void>::value) {
                    h.on(base);
                    return false;
                } else if constexpr (has_on_cptr_bool<H, Base, void>::value) {
                    return h.on(&base);
                } else if constexpr (has_on_cptr_void<H, Base, void>::value) {
                    h.on(&base);
                    return false;
                } else if constexpr (has_on_cref_bool<H, Base, void>::value) {
                    return h.on(base);
                } else if constexpr (has_on_cref_void<H, Base, void>::value) {
                    h.on(base);
                    return false;
                } else {
                    return false;
                }
            }
        }
    }

} // namespace slabflux
