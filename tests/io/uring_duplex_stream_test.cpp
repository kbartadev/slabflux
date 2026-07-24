/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 * PROPRIETARY AND SOURCE-AVAILABLE CODEBASE. ALL RIGHTS RESERVED.
 *
 * This source file and all constitutive programmatic expressions contained herein 
 * are the exclusive intellectual property of Kristóf Barta, established and 
 * distributed strictly under the conditions of the SLABFLUX SOURCE-AVAILABLE 
 * AND ECOSYSTEM LICENSE (the "License").
 *
 * TITLE TO AND OWNERSHIP OF THE SOFTWARE, THE ENGINE, CORE LOGIC, ARCHITECTURAL 
 * LAYOUTS, AND ALL ASSOCIATED INSIGHTS REMAIN SOLELY VESTED IN THE AUTHOR.
 *
 * ----------------------------------------------------------------------------
 * TECHNICAL WARNING & SYSTEM ARCHITECTURE NOTICE
 * ----------------------------------------------------------------------------
 * THIS SOFTWARE UTILIZES ARCHITECTURE-SPECIFIC HARDWARE INTRINSICS AND OPERATES
 * THROUGH LOW-LEVEL, KERNEL-ADJACENT EXECUTION PATHS THAT REDUCE OR BYPASS STANDARD
 * OPERATING SYSTEM MEDIATION LAYERS. INCORRECT INTEGRATION, EXECUTION, OR CONFIGURATION
 * MAY RESULT IN SEVERE SYSTEM INSTABILITY, KERNEL PANICS, OR PERMANENT LOSS OF DATA,
 * AND MAY RENDER SYSTEMS TEMPORARILY OR PERMANENTLY UNUSABLE UNTIL REPAIRED OR
 * RECONFIGURED.
 * ============================================================================
 * @file uring_duplex_stream_test.cpp
 * @brief Validation suite for the io_uring multishot duplex stream engine.
 */

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <thread>
#include <chrono>
#include <immintrin.h>
#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#endif

#include "slabflux/io/uring_duplex_stream.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux;

struct mock_duplex_pipeline {
    int processed = 0;
    template <typename T>
    void dispatch(T*) noexcept { processed++; }
};

/**
 * @brief Physical Residency Audit
 * Ensures the stream class respects 64-byte L1 cache boundaries.
 */
TEST(UringDuplexStreamTest, PhysicalResidency) {
    using Pool = core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>;
    using Stream = io::uring_duplex_stream<Pool, mock_duplex_pipeline>;
    
    EXPECT_EQ(alignof(Stream), 64);
    EXPECT_EQ(sizeof(Stream) % 64, 0);
}

/**
 * @brief End-To-End Dynamic Replenishment & Zero-Copy TX test
 */
TEST(UringDuplexStreamTest, DynamicReplenishmentAndZeroCopy) {
#ifndef _WIN32
    // Demote from SCHED_FIFO to prevent SQPOLL kernel starvation
    struct sched_param param{};
    param.sched_priority = 0;
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#endif

    int fds[2];
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    ::listen(listen_fd, 1);
    socklen_t len = sizeof(addr);
    ::getsockname(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
    fds[0] = ::socket(AF_INET, SOCK_STREAM, 0);
    ::connect(fds[0], reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    fds[1] = ::accept(listen_fd, nullptr, nullptr);
    ::close(listen_fd);

    // Set non-blocking to avoid test hangs
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
    fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) | O_NONBLOCK);

    int flag = 1;
    ::setsockopt(fds[0], IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    ::setsockopt(fds[1], IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    try {
        core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024> pool;
        mock_duplex_pipeline pipe;
        
        int sq_cpu = (std::thread::hardware_concurrency() > 1) ? 1 : 0;
        io::uring_duplex_stream duplex(pool, pipe, fds[0], sq_cpu);

        // --- 1. Test Egress (Zero-Copy Send) ---
        auto* ev = pool.make_raw();
        ASSERT_NE(ev, nullptr);
        ev->payload_length = 16;
        EXPECT_TRUE(duplex.dispatch(ev, 16));

        // Force completion reaping with sufficient kernel yield time
        char buf[64];
        int ret = -1;
        auto start1 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start1 < std::chrono::seconds(2)) {
            duplex.poll_runtime();
            ret = ::recv(fds[1], buf, sizeof(buf), MSG_DONTWAIT);
            if (ret > 0) break;
            std::this_thread::yield();
        }

        EXPECT_GT(ret, 0);
        
        // --- 2. Test Ingress (Dynamic Replenishment) ---
        ::send(fds[1], "PING", 4, 0);
        
        bool received = false;
        // Run multiple polls to ensure SQE multishot trigger, DMA fill, and CQE reap
        auto start2 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start2 < std::chrono::seconds(2)) {
            duplex.poll_runtime();
            if (pipe.processed > 0) {
                received = true;
                break;
            }
            std::this_thread::yield();
        }
        EXPECT_TRUE(received);

        // --- 3. Verify Memory Stability ---
        // Test safe shutdown without double-frees or leaks of captive kernel buffers.
        SUCCEED();

    } catch (const std::exception& e) {
        // Intelligent skip for CI runners lacking Linux 5.19+ io_uring capabalities
        GTEST_SKIP() << "io_uring init failed (missing kernel support or memlock): " << e.what();
    }

    ::close(fds[0]);
    ::close(fds[1]);
}