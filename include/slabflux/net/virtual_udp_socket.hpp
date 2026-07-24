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
 * ============================================================================* @file virtual_udp_socket.hpp
 * @brief High-Level OS-Bypass UDP API mapped to stateless gateway routing.
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <utility>
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::net {

    /**
     * @brief Zero-Allocation Virtual UDP Socket.
     * @details Provides a unified, high-level interface for business logic to emit 
     * connectionless datagrams, abstracting away the underlying gateway topologies.
     */
    template <typename GatewayType>
    class virtual_udp_socket {
    private:
        GatewayType& gateway_;
        uint16_t local_port_;

    public:
        explicit virtual_udp_socket(GatewayType& gateway, uint16_t local_port) noexcept 
            : gateway_(gateway), local_port_(local_port) {}

        [[nodiscard]] SLAB_FORCE_INLINE uint16_t get_local_port() const noexcept {
            return local_port_;
        }

        /**
         * @brief Dispatches a UDP datagram via the Gateway.
         * @details Leverages SFINAE to automatically bind to the correct signature 
         * (Public Gateway with ARP vs Stateless Internal Mesh).
         */
        template <typename... Args>
        SLAB_HOT auto send_to(Args&&... args) noexcept 
            -> decltype(std::declval<GatewayType>().send_udp(std::forward<Args>(args)...)) 
        {
            return gateway_.send_udp(std::forward<Args>(args)...);
        }

        /**
         * @brief Dispatches an IPv6 UDP datagram via the Gateway.
         */
        template <typename... Args>
        SLAB_HOT auto send_to_ipv6(Args&&... args) noexcept 
            -> decltype(std::declval<GatewayType>().send_udp_ipv6(std::forward<Args>(args)...)) 
        {
            return gateway_.send_udp_ipv6(std::forward<Args>(args)...);
        }
    };

} // namespace slabflux::net