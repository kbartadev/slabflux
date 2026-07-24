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

#include <type_traits>
#include <cstddef>
#include "slabflux/pipeline/typelist_algebra.hpp"

namespace slabflux::reflection {
    template <typename T>
    struct meta_traits;
} // namespace slabflux::reflection

namespace slabflux::core {

    // -----------------------------------------------------------------------------
    // 1. parents<T> three-level fallback
    // -----------------------------------------------------------------------------

    // Typelist Normalization (Converts any external List<Ts...> to slabflux::core::typelist<Ts...>)
    template <typename T>
    struct normalize_list {
        using type = T;
    };

    template <template<typename...> class List, typename... Ts>
    struct normalize_list<List<Ts...>> {
        using type = typelist<Ts...>;
    };

    template <typename T>
    using normalize_list_t = typename normalize_list<T>::type;

    // 1) T::parents
    template <typename T, typename = void>
    struct has_direct_parents : std::false_type { };

    template <typename T>
    struct has_direct_parents<T, std::void_t<typename std::remove_cv_t<T>::parents>> : std::true_type { };

    // 2) reflection::meta_traits<T>::parents
    template <typename T, typename = void>
    struct has_meta_parents : std::false_type { };

    template <typename T>
    struct has_meta_parents<T, std::void_t<typename ::slabflux::reflection::meta_traits<std::remove_cv_t<T>>::parents>> : std::true_type { };

    template <typename T, bool = has_direct_parents<T>::value>
    struct parents_of_impl {
        using type = normalize_list_t<typename std::remove_cv_t<T>::parents>;
    };

    template <typename T, bool HasMeta>
    struct meta_parents_extractor {
        using type = typelist<>;
    };

    template <typename T>
    struct meta_parents_extractor<T, true> {
        using type = normalize_list_t<typename ::slabflux::reflection::meta_traits<std::remove_cv_t<T>>::parents>;
    };

    template <typename T>
    struct parents_of_impl<T, false> {
        using type = typename meta_parents_extractor<T, has_meta_parents<T>::value>::type;
    };

    template <typename T>
    using parents_of_t = typename parents_of_impl<T>::type;

    // -----------------------------------------------------------------------------
    // 2. ancestors(T) = unique( concat(T, ancestors(parents(T))) )
    // -----------------------------------------------------------------------------

    template <typename T, typename = void>
    struct ancestors_impl;

    template <typename T>
    struct ancestors_impl<T, std::enable_if_t<list_size<parents_of_t<T>>::value == 0>> {
        using type = typelist<T>;
    };

    template <typename T>
    struct ancestors_impl<T, std::enable_if_t<(list_size<parents_of_t<T>>::value > 0)>> {
    private:
        using parents = parents_of_t<T>;

        template <typename... Ps>
        struct expand;

        template <typename... Ps>
        struct expand<typelist<Ps...>> {
            using type = concat_t<typename ancestors_impl<Ps>::type...>;
        };

        using parent_anc = typename expand<parents>::type;

    public:
        using type = unique_t<concat_t<typelist<T>, parent_anc>>;
    };

    template <typename T>
    using ancestors_t = typename ancestors_impl<T>::type;

    // -----------------------------------------------------------------------------
    // 3. descendant_count(T) relatív egy lokális DAG-listához
    // -----------------------------------------------------------------------------

    template <typename T, typename U>
    struct is_ancestor_of {
        static constexpr bool value = contains_v<T, ancestors_t<U>>;
    };

    template <typename T, typename U>
    inline constexpr bool is_ancestor_of_v = is_ancestor_of<T, U>::value;

    template <typename T, typename List>
    struct descendant_count_in;

    template <typename T>
    struct descendant_count_in<T, typelist<>> : std::integral_constant<std::size_t, 0> { };

    template <typename T, typename U, typename... Us>
    struct descendant_count_in<T, typelist<U, Us...>> {
    private:
        static constexpr std::size_t head =
        is_ancestor_of_v<T, U> ? 1 : 0;
        static constexpr std::size_t tail =
        descendant_count_in<T, typelist<Us...>>::value;
    public:
        static constexpr std::size_t value = head + tail;
    };

    // -----------------------------------------------------------------------------
    // 4. Topological sort (descendant_count DESC, inverse priority ASC)
    // -----------------------------------------------------------------------------

    template <typename T>
    struct effective_priority;

    template <typename T, typename List>
    struct topo_key {
        static constexpr std::size_t desc = descendant_count_in<T, List>::value;
        static constexpr std::size_t prio = effective_priority<T>::value;
    };

    template <typename A, typename B, typename List>
    struct topo_less {
        static constexpr bool value =
        (topo_key<A, List>::desc > topo_key<B, List>::desc) ? true :
        (topo_key<A, List>::desc < topo_key<B, List>::desc) ? false :
        // Priority only overrides at the micro-topological level (diamond/leaf ties)
        (topo_key<A, List>::desc <= 2) ? (topo_key<A, List>::prio < topo_key<B, List>::prio) : false;
    };

    // True Compile-Time Insertion Sort
    template <typename List, typename T, typename FullList> struct topo_insert;

    template <typename T, typename FullList>
    struct topo_insert<typelist<>, T, FullList> { using type = typelist<T>; };

    template <typename U, typename... Us, typename T, typename FullList>
    struct topo_insert<typelist<U, Us...>, T, FullList> {
        using type = std::conditional_t<
            !topo_less<U, T, FullList>::value,
            concat_t<typelist<T>, typelist<U, Us...>>,
            concat_t<typelist<U>, typename topo_insert<typelist<Us...>, T, FullList>::type>
        >;
    };

    template <typename List, typename FullList> struct topo_sort_impl;

    template <typename FullList>
    struct topo_sort_impl<typelist<>, FullList> { using type = typelist<>; };

    template <typename T, typename... Ts, typename FullList>
    struct topo_sort_impl<typelist<T, Ts...>, FullList> {
        using tail_sorted = typename topo_sort_impl<typelist<Ts...>, FullList>::type;
        using type = typename topo_insert<tail_sorted, T, FullList>::type;
    };

    template <typename List>
    struct topo_sort {
        using type = typename topo_sort_impl<List, List>::type;
    };

    template <typename List>
    using topo_sort_t = typename topo_sort<List>::type;

    // -----------------------------------------------------------------------------
    // Leaf-first Event topological sort (descendant_count ASC, inverse priority ASC)
    // -----------------------------------------------------------------------------
    
    template <typename A, typename B, typename List>
    struct leaf_topo_less {
        static constexpr bool value =
        (topo_key<A, List>::desc < topo_key<B, List>::desc) ? true :
        (topo_key<A, List>::desc > topo_key<B, List>::desc) ? false :
        // Priority only overrides at the micro-topological level (diamond/leaf ties)
        (topo_key<A, List>::desc <= 2) ? (topo_key<A, List>::prio < topo_key<B, List>::prio) : false;
    };

    template <typename List, typename T, typename FullList> struct leaf_topo_insert;

    template <typename T, typename FullList>
    struct leaf_topo_insert<typelist<>, T, FullList> { using type = typelist<T>; };

    template <typename U, typename... Us, typename T, typename FullList>
    struct leaf_topo_insert<typelist<U, Us...>, T, FullList> {
        using type = std::conditional_t<
            !leaf_topo_less<U, T, FullList>::value,
            concat_t<typelist<T>, typelist<U, Us...>>,
            concat_t<typelist<U>, typename leaf_topo_insert<typelist<Us...>, T, FullList>::type>
        >;
    };

    template <typename List, typename FullList> struct leaf_topo_sort_impl;

    template <typename FullList>
    struct leaf_topo_sort_impl<typelist<>, FullList> { using type = typelist<>; };

    template <typename T, typename... Ts, typename FullList>
    struct leaf_topo_sort_impl<typelist<T, Ts...>, FullList> {
        using tail_sorted = typename leaf_topo_sort_impl<typelist<Ts...>, FullList>::type;
        using type = typename leaf_topo_insert<tail_sorted, T, FullList>::type;
    };

    template <typename List>
    struct leaf_topo_sort {
        using type = typename leaf_topo_sort_impl<List, List>::type;
    };

    template <typename List>
    using leaf_topo_sort_t = typename leaf_topo_sort<List>::type;

    // -----------------------------------------------------------------------------
    // 5. Event / Handler DAG builder convenience
    // -----------------------------------------------------------------------------

    template <typename Event>
    using event_dag_t = topo_sort_t<ancestors_t<Event>>;

    template <typename Handler>
    using handler_dag_t = topo_sort_t<ancestors_t<Handler>>;

} // namespace slabflux

// -----------------------------------------------------------------------------
// 6. MOC / Manual Inheritance Verification Assertion
// -----------------------------------------------------------------------------
#define EXPECT_INHERITANCE(CLASS, ...) \
    static_assert(std::is_same_v<::slabflux::core::parents_of_t<CLASS>, ::slabflux::core::typelist<__VA_ARGS__>>, \
        "EXPECT_INHERITANCE FAILED: Generated parents list does not match expected parent list for " #CLASS)
