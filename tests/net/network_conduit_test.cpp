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

#include <gtest/gtest.h>
#include <cassert>
#include <chrono>
#include <iostream>

// Core SLABFLUX includes
#include "slabflux/core.hpp"
#include "slabflux/net/network_conduit.hpp"
#include "slabflux/hw/spin_backoff.hpp"

// Windows-specific TCP setup
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#endif

using namespace slabflux;
using namespace slabflux::net;

// 1. SLABFLUX Event Definition
struct tick_data {
    uint64_t sequence;
    double price;
};

struct alignas(64) tick_event {
    static constexpr uint32_t TYPE_ID = 0xA1B2C3D4;
    tick_data data;
    char _pad[4000]; // Added back to ensure multi-MTU packet fragmentation tests work
};

// 2. Node B Sink
struct tick_sink {
    uint64_t expected_seq = 0;
    size_t received_count = 0;

    void on(tick_event& ev) {
        if (ev.data.sequence != expected_seq) {
            std::cerr << "[FATAL] Sequence mismatch! Expected: " << expected_seq
                      << " Got: " << ev.data.sequence << "\n";
            std::abort();
        }
        expected_seq++;
        received_count++;
    }
};

// PHYSICAL TCP LOOPBACK SETUP (Windows / WinSock2 and POSIX)
void setup_loopback_sockets(os_socket_t& fd_A, os_socket_t& fd_B) {
#if defined(_WIN32)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    os_socket_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // OS assigns a free port

    int opt = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&opt), sizeof(opt));
    ::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(listener, 1);

    int addrlen = sizeof(addr);
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addrlen);

    // Node A (Client) connects
    fd_A = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ::connect(fd_A, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    // Node B (Server) accepts
    fd_B = ::accept(listener, nullptr, nullptr);
    ::closesocket(listener);

    // Tuning: Disable Nagle (TCP_NODELAY) and enable Non-blocking (FIONBIO)
    int flag = 1;
    u_long mode = 1;
    ::setsockopt(fd_A, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(flag));
    ::setsockopt(fd_B, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(flag));
    ::ioctlsocket(fd_A, FIONBIO, &mode);
    ::ioctlsocket(fd_B, FIONBIO, &mode);
#else
    os_socket_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // OS assigns a free port

    int opt = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(listener, 1);

    socklen_t addrlen = sizeof(addr);
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &addrlen);

    // Node A (Client) connects
    fd_A = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ::connect(fd_A, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    // Node B (Server) accepts
    fd_B = ::accept(listener, nullptr, nullptr);
    ::close(listener);

    // Tuning: Disable Nagle (TCP_NODELAY) and enable Non-blocking (O_NONBLOCK)
    int flag = 1;
    ::setsockopt(fd_A, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    ::setsockopt(fd_B, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    int flags_A = ::fcntl(fd_A, F_GETFL, 0);
    ::fcntl(fd_A, F_SETFL, flags_A | O_NONBLOCK);

    int flags_B = ::fcntl(fd_B, F_GETFL, 0);
    ::fcntl(fd_B, F_SETFL, flags_B | O_NONBLOCK);
#endif
}

TEST(NetworkConduit, DeterministicTcpRouting) {
    os_socket_t fd_A = SLAB_INVALID_SOCKET;
    os_socket_t fd_B = SLAB_INVALID_SOCKET;
    setup_loopback_sockets(fd_A, fd_B);

    ASSERT_NE(fd_A, SLAB_INVALID_SOCKET) << "Failed to create Node A socket";
    ASSERT_NE(fd_B, SLAB_INVALID_SOCKET) << "Failed to create Node B socket";

    // INITIALIZE NODE A (Producer)
    runtime_domain<tick_event> domain_A;
    auto conduit_A = std::make_unique<slabflux::net::network_conduit_socket<tick_event, 1024>>();
    conduit_A->bind_socket(fd_A);

    // INITIALIZE NODE B (Consumer)
    runtime_domain<tick_event> domain_B;
    tick_sink sink_B;
    auto conduit_B = std::make_unique<slabflux::net::network_conduit_socket<tick_event, 1024>>();
    conduit_B->bind_socket(fd_B);

    constexpr size_t TOTAL_EVENTS = 1'000'000;
    size_t events_pushed = 0;

    auto start_time = std::chrono::high_resolution_clock::now();

    // THE TIGHT POLL LOOP
    uint32_t yield_count = 0;
    while (sink_B.received_count < TOTAL_EVENTS) {
        // SAFETY BRAKE: If the physical connection drops, exit immediately
        ASSERT_TRUE(conduit_A->is_alive() && conduit_B->is_alive())
            << "Conduit died during test at " << sink_B.received_count << " events";

        size_t prev_received = sink_B.received_count;
        bool made_progress = false;

        // 1. Node A: Produce Event
        if (events_pushed < TOTAL_EVENTS) {
            auto ev = domain_A.make<tick_event>();
            if (ev) {
                ev->data.sequence = events_pushed;
                ev->data.price = 150.50 + (events_pushed * 0.01);

                if (conduit_A->push(ev)) {
                    events_pushed++;
                    made_progress = true;
                }
            }
        }

        // 2. O(1) OS Polling
        conduit_A->poll_tx(domain_A);
        conduit_B->poll_rx(domain_B, sink_B);

        if (made_progress || sink_B.received_count > prev_received) {
            yield_count = 0;
        } else {
            slabflux::hw::spin_backoff(yield_count);
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> elapsed = end_time - start_time;

    //std::cout << "[SUCCESS] 1,000,000 events routed deterministically across TCP boundary.\n";
    //std::cout << "Elapsed Time: " << elapsed.count() / 1000.0 << " ms ("
    //          << (TOTAL_EVENTS / (elapsed.count() / 1000000.0)) << " msg/sec)\n";

    EXPECT_EQ(sink_B.received_count, TOTAL_EVENTS);

    // Clean up
#if defined(_WIN32)
    ::closesocket(fd_A);
    ::closesocket(fd_B);
    WSACleanup();
#endif
}
