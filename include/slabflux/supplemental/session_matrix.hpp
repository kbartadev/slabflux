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
 * ============================================================================* @file session_matrix.hpp
 * @brief Supplemental Session Automation Layer.
 */

#pragma once

#include <array>
#include <cstdint>
#include "slabflux/pipeline/context_vault.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::supplemental {

    template <typename List> struct vault_from_list;
    
    template <typename... Ctxs>
    struct vault_from_list<typelist<Ctxs...>> {
        using type = core::context_vault<Ctxs...>;
    };

    /**
     * @brief Supplemental Session Matrix
     * @details Provides true O(1) compile-time allocated context instances.
     * Pre-allocates an array of Vaults at compile-time to ensure zero hidden 
     * dynamic allocation. Solves UDP interleaving segfaults by isolating 
     * simultaneous instances using a session ID mask.
     * 
     * @tparam Capacity Power-of-two number of simultaneous sessions.
     */
    template <size_t Capacity = 65536>
    class alignas(64) session_matrix {
        static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
        
        using VaultType = typename vault_from_list<reflection::all_registered_contexts>::type;
        std::array<VaultType, Capacity> sessions_{};

    public:
        /**
         * @brief The Magic Pipeline Integration Point.
         * @details The Cartesian Pipeline uses C++20 Duck-Typing to detect this method.
         * It automatically routes events with a 'session_id' to their isolated L1-resident 
         * vault, while global events fall back to the master vault at index 0.
         */
        template <typename Ctx, typename Ev>
        SLAB_FORCE_INLINE Ctx& get(const Ev& ev) noexcept {
            if constexpr (requires { ev.session_id; }) {
                return sessions_[ev.session_id & (Capacity - 1)].template get<Ctx>();
            } else {
                return sessions_[0].template get<Ctx>(); // Fallback for global events
            }
        }

        SLAB_FORCE_INLINE VaultType& get_session(uint64_t session_id) noexcept {
            return sessions_[session_id & (Capacity - 1)];
        }

        SLAB_FORCE_INLINE void clear_session(uint64_t session_id) noexcept {
            sessions_[session_id & (Capacity - 1)].clear_all();
        }
    };
} // namespace slabflux::supplemental