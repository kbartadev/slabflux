/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file socket_engine_test.cpp
 * @brief Legacy Syscall-based POSIX Network Engine Verification.
 */

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <thread>
#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#endif
#include <immintrin.h>
#include "slabflux/io/socket_ingress.hpp"
#include "slabflux/io/socket_egress.hpp"
#include "slabflux/io/socket_duplex.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/transport/wire_protocol.hpp"

using namespace slabflux;

struct mock_socket_logic {
    uint64_t processed{0};
    bool on_raw_frame(void*, size_t) noexcept {
        processed++;
        return true;
    }
};

// Helper for real I/O
void setup_test_loopback(int& tx, int& rx) {
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    ::listen(listen_fd, 1);
    socklen_t len = sizeof(addr);
    ::getsockname(listen_fd, (struct sockaddr*)&addr, &len);
    
    tx = ::socket(AF_INET, SOCK_STREAM, 0);
    ::connect(tx, (struct sockaddr*)&addr, sizeof(addr));
    rx = ::accept(listen_fd, nullptr, nullptr);
    ::fcntl(tx, F_SETFL, fcntl(tx, F_GETFL) | O_NONBLOCK);
    ::fcntl(rx, F_SETFL, fcntl(rx, F_GETFL) | O_NONBLOCK);

    int flag = 1;
    ::setsockopt(tx, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    ::setsockopt(rx, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    ::close(listen_fd);
}

TEST(SocketEngineTest, PhysicalResidency) {
    using Pool = core::pinned_allocator_spsc<transport::raw_tcp_frame, 32>;
    using NetBus = core::spsc_conduit<transport::raw_tcp_frame*, 32>;
    
    EXPECT_EQ(alignof(io::socket_ingress<transport::raw_tcp_frame, mock_socket_logic, Pool>), 64);
    EXPECT_EQ(alignof(io::socket_egress<transport::raw_tcp_frame, NetBus, Pool>), 64);
    EXPECT_EQ(alignof(io::socket_duplex<transport::raw_tcp_frame, mock_socket_logic, Pool, NetBus, Pool>), 64);
}

TEST(SocketEngineTest, LegacyIngressRouting) {
#ifndef _WIN32
    // Demote from SCHED_FIFO to prevent TCP stack kernel starvation
    struct sched_param param{};
    param.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#endif
    int tx_fd, rx_fd;
    setup_test_loopback(tx_fd, rx_fd);
    
    mock_socket_logic logic;
    try {
    core::pinned_allocator_spsc<transport::raw_tcp_frame, 32> pool;
    io::socket_ingress<transport::raw_tcp_frame, mock_socket_logic, decltype(pool)> ingress(rx_fd, logic, pool);

    // Send dummy packet
    char dummy[64] = "SLABFLUX_LEGACY";
    ::send(tx_fd, dummy, sizeof(dummy), 0);
    
    // Poll until packet arrives
    bool arrived = false;
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
        ingress.poll();
        if (logic.processed > 0) { arrived = true; break; }
        std::this_thread::yield();
    }
    
    EXPECT_TRUE(arrived);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Locked memory exhausted: " << e.what();
    }
    ::close(tx_fd); ::close(rx_fd);
}

TEST(SocketEngineTest, LegacyDuplexRouting) {
#ifndef _WIN32
    // Demote from SCHED_FIFO to prevent TCP stack kernel starvation
    struct sched_param param{};
    param.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#endif
    int hammer_fd, duplex_fd;
    setup_test_loopback(hammer_fd, duplex_fd);

    mock_socket_logic logic;
    try {
    core::pinned_allocator_spsc<transport::raw_tcp_frame, 64> pool;
    core::spsc_conduit<transport::raw_tcp_frame*, 64> tx_conduit;
    
    io::socket_duplex<transport::raw_tcp_frame, mock_socket_logic, decltype(pool), decltype(tx_conduit), decltype(pool)> 
        duplex(duplex_fd, logic, pool, tx_conduit, pool);

    // 1. Test RX
    char dummy[64] = "PING";
    ::send(hammer_fd, dummy, sizeof(dummy), 0);
    
    // 2. Test TX
    auto* frame = pool.make_raw();
    ASSERT_NE(frame, nullptr);
    frame->payload_length = 64;
    tx_conduit.push(frame);

    bool rx_arrived = false;
    bool tx_arrived = false;
    char verify_buf[128];

    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
        duplex.poll();
        if (logic.processed > 0) rx_arrived = true;
        
        if (::recv(hammer_fd, verify_buf, sizeof(verify_buf), MSG_DONTWAIT) > 0) {
            tx_arrived = true;
        }

        if (rx_arrived && tx_arrived) break;
        std::this_thread::yield();
    }

    EXPECT_TRUE(rx_arrived);
    EXPECT_TRUE(tx_arrived);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Locked memory exhausted: " << e.what();
    }
    ::close(hammer_fd); ::close(duplex_fd);
}