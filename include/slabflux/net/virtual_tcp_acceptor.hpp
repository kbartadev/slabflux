/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#pragma once

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/spsc_ring_conduit.hpp"
#include "slabflux/net/virtual_tcp_socket.hpp"
#include "slabflux/net/tcp_transmission_control_block.hpp"

namespace slabflux::net {

    template <typename GatewayType>
    class alignas(64) virtual_tcp_acceptor {
    private:
        GatewayType& gateway_;
        core::spsc_ring_conduit<uint32_t, 1024>* accept_queue_{nullptr};

    public:
        explicit virtual_tcp_acceptor(GatewayType& gateway, core::spsc_ring_conduit<uint32_t, 1024>* accept_q) noexcept 
            : gateway_(gateway), accept_queue_(accept_q) {}

        /**
         * @brief Polls for natively established connections bridging the gateway to L7 handlers.
         */
        template <typename Handler>
        SLAB_HOT void poll_accept(Handler& handler) noexcept {
            if (SL_EXPECT_FALSE(!accept_queue_)) return;

            while (accept_queue_->available_to_peek() > 0) {
                const uint32_t* conn_id_ptr = accept_queue_->get_peek_slot(0);
                if (SL_EXPECT_TRUE(conn_id_ptr != nullptr)) {
                    uint32_t conn_id = *conn_id_ptr;
                    if (SL_EXPECT_FALSE(conn_id == 0xFFFFFFFF)) {
                        accept_queue_->consume_n(1);
                        continue; // Connection was aborted natively at L3 due to TCB exhaustion
                    }
                    tcp_transmission_control_block& tcb = gateway_.get_tcb(conn_id);
                    virtual_tcp_socket new_sock(&tcb);
                    handler.on_accept(new_sock, conn_id);
                    accept_queue_->consume_n(1);
                } else break;
            }
        }
    };

} // namespace slabflux::net