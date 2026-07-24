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
#include <liburing.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "wire_frame_lsn.hpp"

namespace slabflux::net {

/**
 * @brief Zero-copy Multicast Egress for the Master Sequencer.
 * @details Performance integrity. Broadcasting LSNs to the Board.
 */
template<typename Payload>
class multicast_egress {
    struct io_uring& ring_;
    int socket_fd_;
    union {
        struct sockaddr_in v4;
        struct sockaddr_in6 v6;
    } group_addr_;
    bool is_ipv6_;

public:
    multicast_egress(struct io_uring& ring, const char* multicast_ip, int port) 
        : ring_(ring) {
        
        // Determine Address Family dynamically via string analysis
        if (__builtin_strchr(multicast_ip, ':') != nullptr) {
            is_ipv6_ = true;
            socket_fd_ = socket(AF_INET6, SOCK_DGRAM, 0);
            
            group_addr_.v6.sin6_family = AF_INET6;
            group_addr_.v6.sin6_port = htons(port);
            inet_pton(AF_INET6, multicast_ip, &group_addr_.v6.sin6_addr);

            // Set Multicast Hops (Equivalent to TTL in IPv4)
            int hops = 1;
            setsockopt(socket_fd_, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, sizeof(hops));
        } else {
            is_ipv6_ = false;
            socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
            
            group_addr_.v4.sin_family = AF_INET;
            group_addr_.v4.sin_port = htons(port);
            inet_pton(AF_INET, multicast_ip, &group_addr_.v4.sin_addr);

            // Set TTL and outgoing interface
            int ttl = 1; 
            setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
        }

        // Kernel Routing Bypass
        // SO_DONTROUTE forces the kernel to skip the routing logic entirely.
        int dontroute = 1;
        setsockopt(socket_fd_, SOL_SOCKET, SO_DONTROUTE, &dontroute, sizeof(dontroute));
    }

    /**
     * @brief Blasts the LSN frame to the entire Board.
     * @details Uses io_uring to bypass the synchronous kernel send path.
     */
    inline void broadcast(const wire_frame_lsn<Payload>* frame) noexcept {
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        
        // IORING_OP_SEND is inherently faster than sendto() 
        // when combined with SQPOLL (Kernel-side polling).
        // BUGFIX: Using prep_send overwrites addr with the buffer pointer. 
        // We MUST use io_uring_prep_sendto to correctly separate the payload from the destination.
        if (is_ipv6_) {
            io_uring_prep_sendto(sqe, socket_fd_, frame, sizeof(*frame), MSG_DONTWAIT,
                                 reinterpret_cast<const sockaddr*>(&group_addr_.v6), sizeof(group_addr_.v6));
        } else {
            io_uring_prep_sendto(sqe, socket_fd_, frame, sizeof(*frame), MSG_DONTWAIT,
                                 reinterpret_cast<const sockaddr*>(&group_addr_.v4), sizeof(group_addr_.v4));
        }
        
        // Zero-copy: The NIC will read directly from the Slab memory.
        io_uring_submit(&ring_); 
    }
};

} // namespace slabflux::net