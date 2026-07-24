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
 * @file uring_ingress_stream_test.cpp
 * @brief Functional End-to-End Validation for io_uring Multishot Ingress Stream.
 */

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <immintrin.h>
#ifndef _WIN32
#include <pthread.h>
#include <sched.h>
#endif

#include "slabflux/io/uring_ingress_stream.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/core/hardware_topology.hpp"

using namespace slabflux;

struct mock_ingress_pipeline {
    int processed = 0;
    uint16_t last_len = 0;
    
    template <typename T>
    void dispatch(T* req) noexcept {
        processed++;
        if constexpr (requires { req->payload_length; }) last_len = req->payload_length;
        else if constexpr (requires { req->buffer_length; }) last_len = req->buffer_length;
        else if constexpr (requires { req->length; }) last_len = req->length;
    }
};

TEST(UringIngressStreamTest, EndToEndMultishotRouting) {
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
    
    ::fcntl(fds[0], F_SETFL, ::fcntl(fds[0], F_GETFL) | O_NONBLOCK);
    ::fcntl(fds[1], F_SETFL, ::fcntl(fds[1], F_GETFL) | O_NONBLOCK);

    int flag = 1;
    ::setsockopt(fds[0], IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    ::setsockopt(fds[1], IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    try {
        core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024> pool;
        mock_ingress_pipeline pipe;
        
        int sq_cpu = (std::thread::hardware_concurrency() > 1) ? 1 : 0;
        io::uring_ingress_stream ingress(pool, pipe, sq_cpu);
        ingress.arm_socket(fds[0]);

        // Fire multiple discrete messages to test multishot CQE triggering
        const char* msg1 = "PING";
        const char* msg2 = "PONG";
        ::send(fds[1], msg1, 4, 0);
        ::send(fds[1], msg2, 4, 0);

        bool success = false;
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
            ingress.poll_hot_path();
            if (pipe.processed >= 2) {
                success = true;
                break;
            }
            // AF_UNIX stream sockets might coalesce the two 4-byte sends into one 8-byte recv
            if (pipe.processed == 1 && pipe.last_len == 8) {
                success = true;
                break;
            }
            std::this_thread::yield();
        }

        EXPECT_TRUE(success) << "Failed to route multishot packets through the ingress stream.";
        EXPECT_TRUE(pipe.last_len == 4 || pipe.last_len == 8);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "io_uring initialization failed: " << e.what();
    }

    ::close(fds[0]);
    ::close(fds[1]);
}