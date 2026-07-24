/*
 * SPDX-License-Identifier: AGPL-3.0-only OR LicenseRef-Commercial
 * SLABFLUX RTE - Labs
 */

#pragma once
#include <immintrin.h>
#include "slabflux/core/pipeline.hpp"

namespace slabflux::compute {

    /**
     * @brief Specialized stimulus for physics-based state updates.
     */
    struct alignas(64) stimulus_event {
        float intensity;
        float confidence;

        stimulus_event(float i, float c) : intensity(i), confidence(c) {}
    };

    /**
     * @brief AVX-512 accelerated physics reactor.
     * @details Pass the event destination interface rather than the class itself to satisfy meta.hpp.
     */
    class physics_reactor {
    public:
        static constexpr size_t STATE_SIZE = 16;
        alignas(64) float state_vector[STATE_SIZE]{0.0f};
        float viscosity;

        explicit physics_reactor(float v = 0.0f) : viscosity(v) {}

        /**
         * @brief Pipeline entry point.
         */
        void on(const stimulus_event& ev) noexcept {
            __m512 v_state = _mm512_load_ps(state_vector);
            __m512 v_stim  = _mm512_set1_ps(ev.intensity);
            __m512 v_conf  = _mm512_set1_ps(ev.confidence);
            __m512 v_visc  = _mm512_set1_ps(viscosity);

            // Integration: S_new = (S_old + (I * C)) * (1.0 - Viscosity)
            __m512 v_integrated = _mm512_fmadd_ps(v_stim, v_conf, v_state);
            __m512 v_res = _mm512_mul_ps(v_integrated, _mm512_sub_ps(_mm512_set1_ps(1.0f), v_visc));
            _mm512_store_ps(state_vector, v_res);
        }

        [[nodiscard]] float get_primary_state() const noexcept {
            return state_vector[0];
        }
    };

} // namespace slabflux::compute

namespace slabflux::compute {
    using stimulus_event = compute::stimulus_event;
    using physics_reactor = compute::physics_reactor;
}
