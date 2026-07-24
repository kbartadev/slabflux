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
 * ============================================================================* @file virtual_tcp_listener.hpp
 * @brief Non-blocking Virtual Acceptor.
 */

#pragma once

#include <cstdint>
#include "slabflux/core/spsc_ring_conduit.hpp"
#include "slabflux/net/virtual_tcp_socket.hpp"

namespace slabflux::net {

    class alignas(core::CACHE_LINE_SIZE) virtual_tcp_listener {
        core::spsc_ring_conduit<uint32_t, 1024> backlog_queue_;
        tcp_transmission_control_block* tcbs_;

    public:
        explicit virtual_tcp_listener(tcp_transmission_control_block* tcbs) noexcept : tcbs_(tcbs) {}

        bool listen(uint16_t port) noexcept {
            // Integration: Registers the port in the Nexus Routing Table
            return true;
        }

        /**
         * @brief Non-blocking accept. 
         * @return Valid virtual socket if a connection handshake completed, otherwise invalid.
         */
        virtual_tcp_socket accept() noexcept {
            uint32_t* conn_id = backlog_queue_.get_peek_slot(0);
            if (SL_EXPECT_TRUE(conn_id != nullptr)) {
                uint32_t id = *conn_id;
                backlog_queue_.consume_n(1);
                return virtual_tcp_socket(&tcbs_[id & 1023]);
            }
            return virtual_tcp_socket(nullptr);
        }

        /** @brief Called by Gateway when SYN-ACK/ACK sequence resolves to ESTABLISHED. */
        bool enqueue_established(uint32_t connection_id) noexcept {
            uint32_t* slot = backlog_queue_.get_reserved_slot(0);
            if (SL_EXPECT_TRUE(slot != nullptr)) {
                *slot = connection_id;
                backlog_queue_.commit_n(1);
                return true;
            }
            return false;
        }
    };

} // namespace slabflux::net