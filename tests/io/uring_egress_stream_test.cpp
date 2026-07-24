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
 * @file uring_egress_stream_test.cpp
 * @brief Validation suite for the io_uring discrete egress stream engine.
 */

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <cstring>
#include <immintrin.h>
#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#endif

#include "slabflux/io/uring_egress_stream.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux;

/**
 * @brief Physical Residency Audit
 */
TEST(UringEgressStreamTest, PhysicalResidency) {
    using Pool = core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>;
    using Stream = io::uring_egress_stream<Pool>;
    
    EXPECT_EQ(alignof(Stream), 64);
    EXPECT_EQ(sizeof(Stream) % 64, 0);
}

/**
 * @brief Verifies asynchronous dispatch and doorbell fusion correctness.
 */
TEST(UringEgressStreamTest, DispatchAndDoorbellFusion) {
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
    
    fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
    fcntl(fds[1], F_SETFL, fcntl(fds[1], F_GETFL) | O_NONBLOCK);

    int flag = 1;
    ::setsockopt(fds[0], IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    ::setsockopt(fds[1], IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    try {
        core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024> pool;
        
        int sq_cpu = (std::thread::hardware_concurrency() > 1) ? 1 : 0;
        io::uring_egress_stream egress(pool, fds[0], sq_cpu);

        auto* ev = pool.make_raw();
        ASSERT_NE(ev, nullptr);
        ev->payload_length = 16;
        std::memset(ev->data, 0xBB, 16);

        // Dispatch stores the SQE without waking the kernel (if SQPOLL is asleep)
        EXPECT_TRUE(egress.dispatch(ev, 16));
        
        // Explicit fusion doorbell kick
        egress.flush_doorbell();

        // Reap CQEs and release memory back to the pool
        char buf[64];
        int ret = -1;
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            egress.flush_doorbell();
            egress.poll_completions();
            ret = ::recv(fds[1], buf, sizeof(buf), MSG_DONTWAIT);
            if (ret > 0) break;
            std::this_thread::yield();
        }
        
        EXPECT_GT(ret, 0);

    } catch (const std::exception& e) {
        GTEST_SKIP() << "io_uring init failed: " << e.what();
    }

    ::close(fds[0]);
    ::close(fds[1]);
}