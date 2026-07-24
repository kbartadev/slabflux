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
 * ============================================================================* SlabFlux Inverse Priority Module
 *
 * Task:
 *  - extract priority token from every handler/event type
 *  - if no priority -> implicit infinity (std::size_t max)
 *  - leaves have implicit priority = 0 (absolute precedence)
 *  - duplicate priority among siblings -> static_assert
 *  - sorting key:
 *        1) descendant_count DESC  (more descendants -> forward)
 *        2) inverse priority ASC   (smaller number -> forward)
 */

#pragma once

#include <type_traits>
#include <limits>
#include <cstddef>
#include "slabflux/pipeline/ancestor_expansion.hpp"

namespace slabflux::core {

    // -----------------------------------------------------------------------------
    // 1. priority<N> típus
    // -----------------------------------------------------------------------------

    template <std::size_t N>
    struct priority {
        static constexpr std::size_t value = N;
    };

    // -----------------------------------------------------------------------------
    // 2. priority extraction
    // -----------------------------------------------------------------------------

    template <typename T, typename = void>
    struct has_priority : std::false_type { };

    template <typename T>
    struct has_priority<T, std::void_t<typename T::priority>> : std::true_type { };

    template <typename T>
    inline constexpr bool has_priority_v = has_priority<T>::value;

/**
 * @brief Internal helper to safely extract priority value.
 * Prevents compiler errors when T::priority does not exist.
 */
template <typename T, bool HasPriority = has_priority_v<T>>
struct priority_value_impl {
    static constexpr std::size_t value = std::numeric_limits<std::size_t>::max();
};

template <typename T>
struct priority_value_impl<T, true> {
    static constexpr std::size_t value = T::priority::value;
};

    template <typename T>
    struct priority_value {
    static constexpr std::size_t value = priority_value_impl<T>::value;
    };

    // -----------------------------------------------------------------------------
    // 3. Leaf-priority override (abszolút precedence)
    // -----------------------------------------------------------------------------

    template <typename T>
    struct effective_priority {
        static constexpr std::size_t value = priority_value<T>::value;
    };

    // -----------------------------------------------------------------------------
    // 4. Descendant count (más modul számolja ki)
    // -----------------------------------------------------------------------------

    template <typename T>
    struct descendant_count {
        // This is filled by the ancestors/parents module.
        // Placeholder: everyone is 1.
        static constexpr std::size_t value = 1;
    };

    // -----------------------------------------------------------------------------
    // 5. Rendezési kulcs
    // -----------------------------------------------------------------------------

    template <typename T>
    struct priority_key {
        static constexpr std::size_t desc = descendant_count<T>::value;
        static constexpr std::size_t prio = effective_priority<T>::value;
    };

    // -----------------------------------------------------------------------------
    // 6. Rendezési predikátum
    // -----------------------------------------------------------------------------

    template <typename A, typename B>
    struct priority_less {
        static constexpr bool value =
        (priority_key<A>::desc > priority_key<B>::desc) ? true :
        (priority_key<A>::desc < priority_key<B>::desc) ? false :
        (priority_key<A>::prio < priority_key<B>::prio);
    };

    // -----------------------------------------------------------------------------
    // 7. Duplicate priority detection (sibling szinten)
    // -----------------------------------------------------------------------------

    template <typename A, typename B>
    struct duplicate_priority {
        static constexpr bool value =
        (effective_priority<A>::value == effective_priority<B>::value);
    };

    // A rendező modulnak kell meghívnia sibling-ekre:
    // static_assert(!duplicate_priority<A,B>::value, "Duplicate priority detected");

    // -----------------------------------------------------------------------------
    // 8. priority_sort<Typelist> – minimális bubble-sort (compile-time)
    // -----------------------------------------------------------------------------

    // True Compile-Time Insertion Sort
    template <typename List, typename T> struct prio_insert;

    template <typename T>
    struct prio_insert<typelist<>, T> { using type = typelist<T>; };

    template <typename U, typename... Us, typename T>
    struct prio_insert<typelist<U, Us...>, T> {
        using type = std::conditional_t<
            priority_less<T, U>::value,
            concat_t<typelist<T>, typelist<U, Us...>>,
            concat_t<typelist<U>, typename prio_insert<typelist<Us...>, T>::type>
        >;
    };

    template <typename List> struct priority_sort;

    template <>
    struct priority_sort<typelist<>> { using type = typelist<>; };

    template <typename T, typename... Ts>
    struct priority_sort<typelist<T, Ts...>> {
        using tail_sorted = typename priority_sort<typelist<Ts...>>::type;
        using type = typename prio_insert<tail_sorted, T>::type;
    };

    template <typename List>
    using priority_sort_t = typename priority_sort<List>::type;

} // namespace slabflux::core

namespace slabflux {
    using core::priority;
}
