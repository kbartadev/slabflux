/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file thread_context.hpp
 * @brief Zero-allocation thread-local context generator.
 */

#pragma once

#include <cstddef>

namespace slabflux::core {

    struct thread_context {
        static inline thread_local size_t worker_id{0};
    };

} // namespace slabflux::core