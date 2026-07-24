/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 * 
 * io_uring Component Tests
 * Validates Zero-Copy ingress, egress and duplex streams over a loopback interface.
 */

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <immintrin.h>
#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#endif

#include "slabflux/io/uring_ingress.hpp"
#include "slabflux/io/uring_egress.hpp"
#include "slabflux/io/uring_duplex.hpp"
#include "slabflux/io/uring_ingress_stream.hpp"
#include "slabflux/io/uring_egress_stream.hpp"
#include "slabflux/io/uring_duplex_stream.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/hw/spin_backoff.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux;

class UringTest : public ::testing::Test {
protected:
    int tx_fd = -1;
    int rx_fd = -1;

    void SetUp() override {
        int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(listen_fd, 0) << "Failed to create listen socket";
        
        int opt = 1;
        ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        
        ASSERT_EQ(::bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0) 
            << "Failed to bind loopback socket";
        
        ASSERT_EQ(::listen(listen_fd, 1), 0);
        
        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
        
        tx_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_GE(tx_fd, 0);
        
        ASSERT_EQ(::connect(tx_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)), 0)
            << "Failed to connect to loopback socket";
            
        rx_fd = ::accept(listen_fd, nullptr, nullptr);
        ASSERT_GE(rx_fd, 0);
        
        int flag = 1;
        ::setsockopt(tx_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        ::setsockopt(rx_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        // Set both to non-blocking to prevent test deadlocks
        ::fcntl(tx_fd, F_SETFL, ::fcntl(tx_fd, F_GETFL) | O_NONBLOCK);
        ::fcntl(rx_fd, F_SETFL, ::fcntl(rx_fd, F_GETFL) | O_NONBLOCK);
        
        ::close(listen_fd);
    }

    void TearDown() override {
        if (tx_fd != -1) ::close(tx_fd);
        if (rx_fd != -1) ::close(rx_fd);
    }
};

// Mock logic pipeline for stream engines
struct MockStreamPipeline {
    int processed = 0;
    template <typename T>
    void dispatch(T*) noexcept { 
        processed++;
    }
};

TEST_F(UringTest, StreamDuplexIntegration) {
#ifndef _WIN32
    // Demote from SCHED_FIFO to prevent SQPOLL kernel starvation
    struct sched_param param{};
    param.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#endif
    try {
        core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024> pool;
        MockStreamPipeline pipe;
        
        int sq_cpu = (std::thread::hardware_concurrency() > 1) ? 1 : 0;
        io::uring_duplex_stream duplex(pool, pipe, rx_fd, sq_cpu);
        
        // Send data via tx_fd to be received by duplex engine on rx_fd
        char dummy[512] = "Hello Uring!";
        ::send(tx_fd, dummy, 512, 0);
        
        bool found = false;
        uint32_t yield_count = 0;
        auto start1 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start1 < std::chrono::seconds(2)) {
            duplex.poll_runtime();
            if (pipe.processed > 0) { 
                found = true; 
                break; 
            }
            std::this_thread::yield();
        }
        EXPECT_TRUE(found) << "Failed to receive data on io_uring duplex stream.";
        
        // Dispatch a response back
        auto* ev = pool.make_raw();
        ASSERT_NE(ev, nullptr);
        ev->payload_length = 64;
        std::memset(ev->data, 0xAA, 64);
        EXPECT_TRUE(duplex.dispatch(ev, 64));
        
        char reply[512] = {0};
        int ret = -1;
        yield_count = 0;
        auto start2 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start2 < std::chrono::seconds(2)) {
            duplex.poll_runtime();
            ret = ::recv(tx_fd, reply, 512, MSG_DONTWAIT);
            if (ret > 0) break;
            std::this_thread::yield();
        }
        
        EXPECT_GT(ret, 0) << "Failed to receive egress data from io_uring duplex stream.";
        
    } catch (const std::exception& e) {
        GTEST_SKIP() << "io_uring initialization failed (likely missing kernel support or memlock limit): " << e.what();
    }
}

TEST_F(UringTest, DiscreteStreamIntegration) {
#ifndef _WIN32
    // Demote from SCHED_FIFO to prevent SQPOLL kernel starvation
    struct sched_param param{};
    param.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#endif
    try {
        core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024> pool;
        MockStreamPipeline pipe;
        
        int sq_cpu = (std::thread::hardware_concurrency() > 1) ? 1 : 0;
        io::uring_ingress_stream<decltype(pool), MockStreamPipeline> ingress(pool, pipe, sq_cpu);
        io::uring_egress_stream<decltype(pool)> egress(pool, tx_fd, sq_cpu);
        
        ingress.arm_socket(rx_fd);
        
        // Egress test
        auto* ev = pool.make_raw();
        ASSERT_NE(ev, nullptr);
        ev->payload_length = 64;
        std::memset(ev->data, 0xAA, 64);
        
        EXPECT_TRUE(egress.dispatch(ev, 64));
        egress.flush_doorbell();
        
        // Ingress test
        bool found = false;
        uint32_t yield_count = 0;
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            egress.flush_doorbell();
            egress.poll_completions();
            ingress.poll_hot_path();
            if (pipe.processed > 0) {
                found = true;
                break;
            }
            std::this_thread::yield();
        }
        EXPECT_TRUE(found) << "Failed to route traffic across discrete io_uring stream engines.";
        
    } catch (const std::exception& e) {
        GTEST_SKIP() << "io_uring initialization failed: " << e.what();
    }
}

TEST_F(UringTest, CoreDuplexIntegration) {
#ifndef _WIN32
    // Demote from SCHED_FIFO to prevent SQPOLL kernel starvation
    struct sched_param param{};
    param.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#endif
    using NetBus = core::spsc_conduit<core::tagged_pointer, 1024>;
    using Pool = core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>;
    
    NetBus in_bus, out_bus;
    std::atomic<bool> running{true};
    
    try {
        Pool pool;
        io::uring_duplex<NetBus, NetBus, Pool, 256> duplex(rx_fd, in_bus, out_bus, pool, running);
        
        
        // Run loop to dispatch and receive CQEs
        char reply[512];
        int ret = -1;
        uint32_t yield_count = 0;
        auto start1 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start1 < std::chrono::seconds(2)) {
            // Actively replenish the conduit to trigger internal io_uring batch submissions
            if (out_bus.occupancy() < 128) {
                for (int i = 0; i < 32; ++i) {
                    auto* ev = pool.make_raw();
                    if (ev) {
                        ev->payload_length = 64;
                        ev->connection_id = static_cast<uint32_t>(rx_fd);
                        std::memset(ev->data, 0xCC, 64);
                        if (!out_bus.try_push(core::tagged_pointer::pack(1, ev))) {
                            pool.release(ev);
                        }
                    }
                }
            }

            duplex.poll_runtime();
            ret = ::recv(tx_fd, reply, 512, MSG_DONTWAIT);
            if (ret > 0) break;
            std::this_thread::yield();
        }
        
        EXPECT_GT(ret, 0) << "Conduit duplex egress failed to transmit data.";

        // Drain the rest of the burst to keep the loopback interface clean for the RX test
        while (::recv(tx_fd, reply, 512, MSG_DONTWAIT) > 0) {}

        // RX test
        ::send(tx_fd, "Ping", 5, 0);
        
        bool found = false;
        yield_count = 0;
        auto start2 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start2 < std::chrono::seconds(2)) {
            duplex.poll_runtime();
            if (in_bus.occupancy() > 0) {
                found = true;
                break;
            }
            std::this_thread::yield();
        }
        EXPECT_TRUE(found) << "Conduit duplex ingress failed to receive data.";
        
        // Clean termination
        running.store(false, std::memory_order_release);
        
    } catch (const std::exception& e) {
        GTEST_SKIP() << "io_uring initialization failed: " << e.what();
    }
}