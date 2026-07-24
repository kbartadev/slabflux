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
 * ============================================================================* SlabFlux Typelist Algebra Module
 *
 * The basis for the entire dispatcher, context-vault, phase-engine, and unroller.
 * Minimal but full-featured typelist algebra:
 *
 *   typelist<Ts...>
 *   list_size
 *   list_at
 *   concat
 *   unique
 *   contains
 *   map
 *   filter
 *
 * All further SlabFlux modules are built on this.
 */

#pragma once

#include <type_traits>
#include <utility>

namespace slabflux::core {

    // -----------------------------------------------------------------------------
    // 1. typelist<Ts...>
    // -----------------------------------------------------------------------------

    template <typename... Ts>
    struct typelist { };

    // -----------------------------------------------------------------------------
    // 2. list_size<typelist<Ts...>>
    // -----------------------------------------------------------------------------

    template <typename List>
    struct list_size;

    template <typename... Ts>
    struct list_size<typelist<Ts...>>
    : std::integral_constant<std::size_t, sizeof...(Ts)> { };

    // -----------------------------------------------------------------------------
    // 3. list_at<I, typelist<Ts...>>
    // -----------------------------------------------------------------------------

    template <std::size_t I, typename List>
    struct list_at;

    template <std::size_t I, typename T, typename... Ts>
    struct list_at<I, typelist<T, Ts...>> : list_at<I - 1, typelist<Ts...>> { };

    template <typename T, typename... Ts>
    struct list_at<0, typelist<T, Ts...>> { using type = T; };

    template <std::size_t I, typename List>
    using list_at_t = typename list_at<I, List>::type;

    // -----------------------------------------------------------------------------
    // 4. contains<T, typelist<...>>
    // -----------------------------------------------------------------------------

    template <typename T, typename List>
    struct contains;

    template <typename T>
    struct contains<T, typelist<>> : std::false_type { };

    template <typename T, typename U, typename... Us>
    struct contains<T, typelist<U, Us...>>
    : std::conditional_t<std::is_same_v<T, U>,
    std::true_type,
    contains<T, typelist<Us...>>> { };

    template <typename T, typename List>
    inline constexpr bool contains_v = contains<T, List>::value;

    // -----------------------------------------------------------------------------
    // 5. concat<typelist<...>, typelist<...>, ...>
    // -----------------------------------------------------------------------------

    template <typename... Lists>
    struct concat;

    template <>
    struct concat<> { using type = typelist<>; };

    template <typename... Ts>
    struct concat<typelist<Ts...>> { using type = typelist<Ts...>; };

    template <typename... Ts, typename... Us, typename... Rest>
    struct concat<typelist<Ts...>, typelist<Us...>, Rest...> {
        using type = typename concat<typelist<Ts..., Us...>, Rest...>::type;
    };

    template <typename... Lists>
    using concat_t = typename concat<Lists...>::type;

    // -----------------------------------------------------------------------------
    // 6. unique<typelist<...>>
    // -----------------------------------------------------------------------------

    template <typename List>
    struct unique;

    template <>
    struct unique<typelist<>> { using type = typelist<>; };

    template <typename T, typename... Ts>
    struct unique<typelist<T, Ts...>> {
    private:
        using tail_unique = typename unique<typelist<Ts...>>::type;

        template <typename... Us>
        static auto prepend_if_not_contains(typelist<Us...>) {
            if constexpr (contains_v<T, typelist<Us...>>)
                return typelist<Us...>{};
            else
                return typelist<T, Us...>{};
        }

    public:
        using type = decltype(prepend_if_not_contains(tail_unique{}));
    };

    template <typename List>
    using unique_t = typename unique<List>::type;

    // -----------------------------------------------------------------------------
    // 7. map<typelist<Ts...>, F>
    // -----------------------------------------------------------------------------

    template <typename List, template <typename> class F>
    struct map;

    template <template <typename> class F, typename... Ts>
    struct map<typelist<Ts...>, F> {
        using type = typelist<typename F<Ts>::type...>;
    };

    template <typename List, template <typename> class F>
    using map_t = typename map<List, F>::type;

    // -----------------------------------------------------------------------------
    // 8. filter<typelist<Ts...>, Pred>
    // -----------------------------------------------------------------------------

    template <typename List, template <typename> class Pred>
    struct filter;

    template <template <typename> class Pred>
    struct filter<typelist<>, Pred> {
        using type = typelist<>;
    };

    template <typename T, typename... Ts, template <typename> class Pred>
    struct filter<typelist<T, Ts...>, Pred> {
    private:
        using tail = typename filter<typelist<Ts...>, Pred>::type;

    public:
        using type = std::conditional_t<
        Pred<T>::value,
        concat_t<typelist<T>, tail>,
        tail
        >;
    };

    template <typename List, template <typename> class Pred>
    using filter_t = typename filter<List, Pred>::type;

} // namespace slabflux::core

namespace slabflux {
    using core::typelist;
}
