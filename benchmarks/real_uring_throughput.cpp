/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 */

#include <benchmark/benchmark.h>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>

#include "slabflux/io/uring_ingress.hpp"
#include "slabflux/io/uring_egress.hpp"
#include "slabflux/io/uring_duplex.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/transport/wire_protocol.hpp"

using namespace slabflux;

static void setup_loopback(int& tx, int& rx) {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_fd, 1);
    socklen_t len = sizeof(addr);
    getsockname(listen_fd, (struct sockaddr*)&addr, &len);
    
    tx = socket(AF_INET, SOCK_STREAM, 0);
    connect(tx, (struct sockaddr*)&addr, sizeof(addr));
    rx = accept(listen_fd, nullptr, nullptr);
    
    fcntl(tx, F_SETFL, fcntl(tx, F_GETFL) | O_NONBLOCK);
    fcntl(rx, F_SETFL, fcntl(rx, F_GETFL) | O_NONBLOCK);
    
    setsockopt(tx, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    setsockopt(rx, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    close(listen_fd);
}

static void BM_Real_Uring_Ingress(benchmark::State& state) {
    using NetBus = core::spsc_conduit<core::tagged_pointer, 4096>;
    NetBus in_bus;
    std::atomic<bool> running{true};
    int tx_fd, rx_fd;
    setup_loopback(tx_fd, rx_fd);

    // Background thread: Traffic Generator (OS TCP send)
    std::thread traffic_generator([&]() {
        char dummy[512] = {0};
        while (running.load(std::memory_order_relaxed)) {
            ::send(tx_fd, dummy, sizeof(dummy), MSG_NOSIGNAL);
            _mm_pause();
        }
    });

    io::uring_ingress<NetBus, 2048> ingress(rx_fd, in_bus, running);
    uint64_t actual_items = 0;
    core::tagged_pointer tokens[32];

    for (auto _ : state) {
        ingress.poll_ingress();
        
        size_t popped = in_bus.pop_batch(tokens, 32);
        if (popped > 0) {
            actual_items += popped;
            ingress.release_ingress_buffers_batch(tokens, popped);
        } else {
            state.PauseTiming();
            _mm_pause();
            state.ResumeTiming();
        }
    }

    running = false;
    traffic_generator.join();
    close(tx_fd); close(rx_fd);

    state.SetBytesProcessed(actual_items * 512);
    state.SetItemsProcessed(actual_items);
}
BENCHMARK(BM_Real_Uring_Ingress)->Unit(benchmark::kNanosecond);

static void BM_Real_Uring_Egress(benchmark::State& state) {
    using NetBus = core::spsc_conduit<core::tagged_pointer, 4096>;
    using Pool = core::pinned_allocator_spsc<transport::raw_tcp_frame, 4096>;
    NetBus out_bus;
    Pool pool;
    std::atomic<bool> running{true};
    int tx_fd, rx_fd;
    setup_loopback(tx_fd, rx_fd);

    // Background thread: Traffic Sink (OS TCP recv)
    std::thread traffic_sink([&]() {
        char buf[65536];
        while (running.load(std::memory_order_relaxed)) {
            while (::recv(rx_fd, buf, sizeof(buf), 0) > 0) {}
            _mm_pause();
        }
    });

    io::uring_egress<NetBus, Pool, 1024> egress(tx_fd, out_bus, pool, running);
    uint64_t actual_items = 0;
    transport::raw_tcp_frame* alloc_batch[32];
    core::tagged_pointer tokens[32];

    for (auto _ : state) {
        size_t n = pool.make_batch(alloc_batch, 32);
        if (n > 0) {
            for (size_t i = 0; i < n; ++i) {
                alloc_batch[i]->payload_length = 512;
                tokens[i] = core::tagged_pointer::pack(1, alloc_batch[i]);
            }
            size_t pushed = out_bus.push_batch(tokens, n);
            if (pushed < n) pool.release_batch(alloc_batch + pushed, n - pushed);
            actual_items += pushed;
        }

        egress.poll_egress();

        if (n == 0) {
            state.PauseTiming();
            _mm_pause();
            state.ResumeTiming();
        }
    }

    running = false;
    traffic_sink.join();
    close(tx_fd); close(rx_fd);

    state.SetBytesProcessed(actual_items * 512);
    state.SetItemsProcessed(actual_items);
}
BENCHMARK(BM_Real_Uring_Egress)->Unit(benchmark::kNanosecond);

static void BM_Real_Uring_Duplex(benchmark::State& state) {
    using NetBus = core::spsc_conduit<core::tagged_pointer, 4096>;
    using Pool = core::pinned_allocator_spsc<transport::raw_tcp_frame, 4096>;
    NetBus in_bus, out_bus;
    Pool pool;
    std::atomic<bool> running{true};
    int tx_fd, rx_fd;
    setup_loopback(tx_fd, rx_fd);

    // Background Echo (OS TCP recv & send)
    std::thread traffic_echo([&]() {
        char buf[8192];
        while (running.load(std::memory_order_relaxed)) {
            int len = ::recv(rx_fd, buf, sizeof(buf), 0);
            if (len > 0) { ::send(rx_fd, buf, len, MSG_NOSIGNAL); }
            else _mm_pause();
        }
    });

    io::uring_duplex<NetBus, NetBus, Pool, 1024> duplex(tx_fd, in_bus, out_bus, pool, running);
    uint64_t actual_items = 0;
    core::tagged_pointer tokens[32];

    for (auto _ : state) {
        duplex.poll_runtime();

        size_t popped = in_bus.pop_batch(tokens, 32);
        if (popped > 0) {
            actual_items += popped;
            duplex.release_ingress_buffers_batch(tokens, popped);
        } else {
            state.PauseTiming();
            _mm_pause();
            state.ResumeTiming();
        }
    }

    running = false;
    traffic_echo.join();
    close(tx_fd); close(rx_fd);

    state.SetBytesProcessed(actual_items * 512);
    state.SetItemsProcessed(actual_items);
}
BENCHMARK(BM_Real_Uring_Duplex)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();