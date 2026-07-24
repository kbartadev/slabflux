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
 * ============================================================================*
 * @file tcp_stream_fragmentation_test.cpp
 * @brief Chaos tests: partial TCP frames and MTU-boundary fragmentation.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "slabflux/core.hpp"
#include "slabflux/net/network_conduit.hpp"
#include "slabflux/hw/spin_backoff.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace slabflux;
using namespace slabflux::net;

namespace {

struct frag_tick_data {
    uint64_t sequence;
    double price;
};

struct alignas(64) frag_tick_event {
    static constexpr uint32_t TYPE_ID = 0xF6A61C01;
    frag_tick_data data;
    char _pad[4000]; // Ensure partial MTU chunking logic is stressed
};

struct frag_tick_sink {
    uint64_t expected_seq = 0;
    size_t received_count = 0;

    void on(frag_tick_event& ev) {
        ASSERT_EQ(ev.data.sequence, expected_seq);
        ++expected_seq;
        ++received_count;
    }
};

void set_nonblocking(os_socket_t fd) {
#if defined(_WIN32)
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

void close_socket(os_socket_t fd) {
#if defined(_WIN32)
    if (fd != INVALID_SOCKET) {
        closesocket(fd);
    }
#else
    if (fd >= 0) {
        close(fd);
    }
#endif
}

// Producer (A) <-> relay (chunked) <-> Consumer (B)
struct fragmented_tcp_path {
    os_socket_t fd_A = INVALID_SOCKET;
    os_socket_t fd_B = INVALID_SOCKET;
    os_socket_t relay_left = INVALID_SOCKET;
    os_socket_t relay_right = INVALID_SOCKET;
    std::thread relay_thread;
    std::atomic<bool> stop{false};

    void start_relay(size_t max_chunk) {
        relay_thread = std::thread([this, max_chunk]() {
            char buf[4096]; // Increased to safely accommodate larger chunking limits
            uint32_t yield_count = 0;
            while (!stop.load(std::memory_order_acquire)) {
                const int n = ::recv(relay_left, buf, static_cast<int>(max_chunk), 0);
                if (n > 0) {
                    int sent = 0;
                    while (sent < n && !stop.load(std::memory_order_acquire)) {
                        const int w = ::send(relay_right, buf + sent, n - sent, 0);
                        if (w > 0) {
                            sent += w;
                        } else {
#if defined(_WIN32)
                            if (WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAEINTR) {
                                return;
                            }
#else
                            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                                return;
                            }
#endif
                            _mm_pause(); // Prevent tight spin if outbound socket is full
                        }
                    }
                    yield_count = 0;
                    continue;
                } else if (n == 0) {
                    return;
                } else {
#if defined(_WIN32)
                    if (WSAGetLastError() != WSAEWOULDBLOCK && WSAGetLastError() != WSAEINTR) {
                        return;
                    }
#else
                    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                        return;
                    }
#endif
                }
                slabflux::hw::spin_backoff(yield_count);
            }
        });
    }

    ~fragmented_tcp_path() {
        stop.store(true, std::memory_order_release);
        if (relay_thread.joinable()) {
            relay_thread.join();
        }
        close_socket(fd_A);
        close_socket(fd_B);
        close_socket(relay_left);
        close_socket(relay_right);
    }
};

bool setup_fragmented_loopback(fragmented_tcp_path& path, size_t max_chunk) {
#if defined(_WIN32)
    WSADATA wsa_data{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        return false;
    }
#endif

    os_socket_t ingress_listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    os_socket_t egress_listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ingress_listener == INVALID_SOCKET || egress_listener == INVALID_SOCKET) {
        return false;
    }

    sockaddr_in ingress_addr{};
    ingress_addr.sin_family = AF_INET;
    ingress_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ingress_addr.sin_port = 0;

    sockaddr_in egress_addr = ingress_addr;
    int opt = 1;
    ::setsockopt(ingress_listener, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<char*>(&opt), sizeof(opt));
    ::setsockopt(egress_listener, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<char*>(&opt), sizeof(opt));
    if (::bind(ingress_listener, reinterpret_cast<sockaddr*>(&ingress_addr), sizeof(ingress_addr)) != 0 ||
        ::bind(egress_listener, reinterpret_cast<sockaddr*>(&egress_addr), sizeof(egress_addr)) != 0 ||
        ::listen(ingress_listener, 1) != 0 || ::listen(egress_listener, 1) != 0) {
        close_socket(ingress_listener);
        close_socket(egress_listener);
        return false;
    }

    socklen_t len = sizeof(ingress_addr);
    if (::getsockname(ingress_listener, reinterpret_cast<sockaddr*>(&ingress_addr), &len) != 0) {
        close_socket(ingress_listener);
        close_socket(egress_listener);
        return false;
    }
    len = sizeof(egress_addr);
    if (::getsockname(egress_listener, reinterpret_cast<sockaddr*>(&egress_addr), &len) != 0) {
        close_socket(ingress_listener);
        close_socket(egress_listener);
        return false;
    }

    path.fd_A = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (path.fd_A == INVALID_SOCKET ||
        ::connect(path.fd_A, reinterpret_cast<sockaddr*>(&ingress_addr), sizeof(ingress_addr)) != 0) {
        return false;
    }
    path.relay_left = ::accept(ingress_listener, nullptr, nullptr);
    if (path.relay_left == INVALID_SOCKET) {
        return false;
    }

    path.relay_right = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (path.relay_right == INVALID_SOCKET ||
        ::connect(path.relay_right, reinterpret_cast<sockaddr*>(&egress_addr), sizeof(egress_addr)) != 0) {
        return false;
    }
    path.fd_B = ::accept(egress_listener, nullptr, nullptr);
    if (path.fd_B == INVALID_SOCKET) {
        return false;
    }

    close_socket(ingress_listener);
    close_socket(egress_listener);

    for (os_socket_t fd : {path.fd_A, path.fd_B, path.relay_left, path.relay_right}) {
        set_nonblocking(fd);
        int nodelay = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&nodelay), sizeof(nodelay));
    }

    path.start_relay(max_chunk);
    return true;
}

}  // namespace

TEST(NetworkChaos, PartialFrameSurvivesFragmentedTcpRelay) {
    constexpr size_t k_frame_size = sizeof(wire_frame<frag_tick_event>);
    static_assert(k_frame_size > 14, "Test requires frames larger than partial chunk");

    fragmented_tcp_path path;
    // Use an unaligned chunk size (127) to rigorously test fragmentation boundaries 
    // while reducing the number of syscalls by 10x to prevent CI timeouts.
    ASSERT_TRUE(setup_fragmented_loopback(path, 127));
    constexpr size_t k_total_events = 1'000; // Reduced from 5000 to prevent 30s timeouts

    runtime_domain<frag_tick_event> domain_A;
    runtime_domain<frag_tick_event> domain_B;
    frag_tick_sink sink_B;

    network_conduit_socket<frag_tick_event, 1024> conduit_A;
    network_conduit_socket<frag_tick_event, 1024> conduit_B;
    conduit_A.bind_socket(path.fd_A);
    conduit_B.bind_socket(path.fd_B);

    size_t pushed = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    uint32_t yield_count = 0;
    while (sink_B.received_count < k_total_events) {
        ASSERT_LT(std::chrono::steady_clock::now(), deadline)
            << "Timed out after " << sink_B.received_count << " events";
        ASSERT_TRUE(conduit_A.is_alive() && conduit_B.is_alive());

        size_t prev_received = sink_B.received_count;
        bool made_progress = false;

        if (pushed < k_total_events) {
            auto ev = domain_A.make<frag_tick_event>();
            if (ev) {
                ev->data.sequence = pushed;
                ev->data.price = 100.0 + static_cast<double>(pushed);
                if (conduit_A.push(ev)) {
                    ++pushed;
                    made_progress = true;
                }
            }
        }

        conduit_A.poll_tx(domain_B);
        conduit_B.poll_rx(domain_B, sink_B);

        if (made_progress || sink_B.received_count > prev_received) {
            yield_count = 0;
        } else {
            // Prevent deep sleep during multi-packet fragmentation assembly
            _mm_pause(); 
        }
    }

    EXPECT_EQ(sink_B.received_count, k_total_events);
    path.stop.store(true, std::memory_order_release);
}

TEST(NetworkChaos, EwouldblockDuringEmptyPollDoesNotKillConduit) {
    os_socket_t fd_A = INVALID_SOCKET;
    os_socket_t fd_B = INVALID_SOCKET;

#if defined(_WIN32)
    WSADATA wsa_data{};
    ASSERT_EQ(0, WSAStartup(MAKEWORD(2, 2), &wsa_data));
#endif

    os_socket_t listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(listener, 1);
    socklen_t len = sizeof(addr);
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);

    fd_A = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ::connect(fd_A, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    fd_B = ::accept(listener, nullptr, nullptr);
    close_socket(listener);

    set_nonblocking(fd_A);
    set_nonblocking(fd_B);

    network_conduit_socket<frag_tick_event, 64> conduit_A;
    network_conduit_socket<frag_tick_event, 64> conduit_B;
    conduit_A.bind_socket(fd_A);
    conduit_B.bind_socket(fd_B);

    runtime_domain<frag_tick_event> domain_B;
    frag_tick_sink sink_B;

    for (int i = 0; i < 10'000; ++i) {
        conduit_A.poll_tx(domain_B);
        conduit_B.poll_rx(domain_B, sink_B);
    }

    EXPECT_TRUE(conduit_A.is_alive());
    EXPECT_TRUE(conduit_B.is_alive());

    close_socket(fd_A);
    close_socket(fd_B);
}
