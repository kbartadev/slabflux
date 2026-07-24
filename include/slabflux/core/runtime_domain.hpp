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

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <new>
#include <utility>
#include <tuple>
#include <array>
#include <vector>
#include <concepts>
#include <type_traits>
#include <cstring>
#include <bit>
#include <memory>
#include "slabflux/core/pool.hpp"

namespace slabflux::core {

    // ===========================================================;
    // LAYER 9: RUNTIME DOMAIN & ROUTING
    // ===========================================================;

    template<std::size_t Capacity, typename... Events>
    class runtime_domain_base {
        std::tuple<pool<Events, Capacity>...> pools_;
    public:
        runtime_domain_base() = default;

        template<typename Event, typename... Args>
        [[nodiscard]] auto make(Args&&... args) {
            auto& p = std::get<pool<Event, Capacity>>(pools_);
            return p.make(std::forward<Args>(args)...);
        }

        template<typename Event>
        pool<Event, Capacity>& get_pool() { return std::get<pool<Event, Capacity>>(pools_); }

        template<typename Event>
        void release(Event* ev) {
            auto& p = std::get<pool<Event, Capacity>>(pools_);
            p.release(ev);
        }
    };

    // Default runtime domain with 1024 slots per event type
    template<typename... Events>
    using runtime_domain = runtime_domain_base<1024, Events...>;
}
