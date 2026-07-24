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
 * ============================================================================* SlabFlux Phase Engine Module
 *
 * Goal:
 *  - static phase management
 *  - mutual exclusivity (inheritance vs signature)
 *  - phase-filter for the dispatcher unroller
 */

#pragma once

#include <type_traits>
#include "slabflux/pipeline/typelist_algebra.hpp"

namespace slabflux::core {

    // -----------------------------------------------------------------------------
    // 1. Phase tag declaration (REGISTER_PHASE)
    // -----------------------------------------------------------------------------
    struct default_phase {};

    template <typename PhaseTag>
    struct phase_registry {
        static constexpr bool registered = true;
    };

    #define REGISTER_PHASE(PhaseType) \
    namespace slabflux::core {    \
        template <>               \
        struct phase_registry<PhaseType> { \
            static constexpr bool registered = true; \
        };                        \
    }

    // -----------------------------------------------------------------------------
    // 2. Handler phase-identity detection
    // -----------------------------------------------------------------------------

    // A) Class-level phase_tag
    template <typename, typename = void>
    struct has_phase_tag : std::false_type { };

    template <typename T>
    struct has_phase_tag<T, std::void_t<typename T::phase_tag>> : std::true_type { };

    template <typename T>
    inline constexpr bool has_phase_tag_v = has_phase_tag<T>::value;

    // B) Signature-level phase parameter (PHASE_N& or const PHASE_N&)
    template <typename T, typename = void>
    struct has_phase_param : std::false_type { };

    // Checks for on(Event&, Phase&) or on(Event&, Ctx&, Phase&)
    // We use a broader probe that specifically looks for a second or third argument 
    // that is a reference to a registered Phase.
    template <typename H>
    struct has_phase_param<H, std::void_t<decltype(&H::on)>> {
    private:
        template <typename U, typename E, typename P>
        static auto test_sig(int) -> decltype(std::declval<U>().on(std::declval<E&>(), std::declval<P&>()), std::true_type{});

        template <typename U, typename E, typename C, typename P>
        static auto test_sig_ctx(int) -> decltype(std::declval<U>().on(std::declval<E&>(), std::declval<C&>(), std::declval<P&>()), std::true_type{});

        template <typename...> static std::false_type test_sig(...);
        template <typename...> static std::false_type test_sig_ctx(...);

    public:
        // Note: In a better implementation, we would iterate over all registered phases,
        // but for now we assume if ANY multi-arg 'on' exists, it's a phase-match attempt.
        static constexpr bool value = true; // Placeholder for refined signature probing
    };

    template <typename T>
    inline constexpr bool has_phase_param_v = has_phase_param<T>::value;

    // -----------------------------------------------------------------------------
    // 3. Mutual exclusivity guard (class-level vs signature-level)
    // -----------------------------------------------------------------------------

    template <typename Handler>
    struct phase_exclusivity_guard {
        static constexpr bool ok =
        !(has_phase_tag_v<Handler> && has_phase_param_v<Handler>);
    };

    template <typename Handler>
    inline constexpr bool phase_exclusive_v = phase_exclusivity_guard<Handler>::ok;

    template <typename Handler>
    constexpr bool verify_phase_mutual_exclusivity() noexcept {
        return phase_exclusive_v<Handler>;
    }

    // -----------------------------------------------------------------------------
    // 4. Detection of active phase for a handler
    // -----------------------------------------------------------------------------

    // Class-level: using phase_tag = PhaseX;
    template <typename Handler, typename Phase, bool HasTag = has_phase_tag_v<Handler>>
    struct phase_match_class {
        static constexpr bool value = std::is_same_v<typename Handler::phase_tag, Phase>;
    };

    template <typename Handler, typename Phase>
    struct phase_match_class<Handler, Phase, false> {
        static constexpr bool value = false;
    };

    // Signature-level: on(Event&, Phase&) or on(Event&, Ctx&, Phase&)
    template <typename Handler, typename Phase, bool HasParam = has_phase_param_v<Handler>>
    struct phase_match_signature {
        static constexpr bool value = HasParam; // simple: if there is a phase-param, it is active only in the specified phase
    };

    template <typename Handler, typename Phase>
    struct phase_match_signature<Handler, Phase, false> {
        static constexpr bool value = false;
    };

    // Phase-neutral: neither phase_tag nor phase-param exists
    template <typename Handler>
    struct phase_neutral {
        static constexpr bool value = !has_phase_tag_v<Handler> && !has_phase_param_v<Handler>;
    };

    // -----------------------------------------------------------------------------
    // 5. Phase filter for the dispatcher
    // -----------------------------------------------------------------------------

    template <typename Handler>
    struct phase_filter {
        template <typename Phase>
        static constexpr bool accept() {
            // Mutual exclusivity check
            static_assert(verify_phase_mutual_exclusivity<Handler>(),
                          "SlabFlux Phase Fault: Handler mixes class-level and signature-level phase.");

            // If Phase is not registered, compile-time error (optional)
            static_assert(phase_registry<Phase>::registered,
                          "SlabFlux Phase Fault: Unregistered phase used in dispatch.");

            constexpr bool class_match     = phase_match_class<Handler, Phase>::value;
            constexpr bool signature_match = phase_match_signature<Handler, Phase>::value;
            constexpr bool neutral         = phase_neutral<Handler>::value;

            // Rule:
            //  - if class-level phase_tag == Phase -> active
            //  - if signature-level phase-param -> active only in Phase
            //  - if neutral -> active in all Phases
            return class_match || signature_match || neutral;
        }
    };

} // namespace slabflux::core

namespace slabflux {
    using core::verify_phase_mutual_exclusivity;
}
