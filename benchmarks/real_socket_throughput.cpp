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
#include <immintrin.h>

#include "slabflux/io/socket_ingress.hpp"
#include "slabflux/io/socket_egress.hpp"
#include "slabflux/io/socket_duplex.hpp"
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
    close(listen_fd);
}

struct mock_logic {
    std::atomic<uint64_t> count{0};
    bool on_raw_frame(void*, size_t) noexcept { 
        count.fetch_add(1, std::memory_order_relaxed);
        return true; 
    }
};

static void BM_Real_Socket_Ingress(benchmark::State& state) {
    mock_logic logic;
    using Pool = core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>;
    Pool pool;
    int tx_fd, rx_fd;
    setup_loopback(tx_fd, rx_fd);

    std::atomic<bool> running{true};
    std::thread traffic_generator([&]() {
        char dummy[512] = {0};
        while (running.load(std::memory_order_relaxed)) {
            ::send(tx_fd, dummy, sizeof(dummy), MSG_NOSIGNAL);
            _mm_pause();
        }
    });

    io::socket_ingress<transport::raw_tcp_frame, mock_logic, Pool> ingress(rx_fd, logic, pool);

    for (auto _ : state) {
        uint64_t before = logic.count.load(std::memory_order_relaxed);
        ingress.poll();
        if (logic.count.load(std::memory_order_relaxed) == before) {
            state.PauseTiming(); _mm_pause(); state.ResumeTiming();
        }
    }

    running = false;
    traffic_generator.join();
    close(tx_fd); close(rx_fd);
    state.SetBytesProcessed(logic.count.load() * 512);
    state.SetItemsProcessed(logic.count.load());
}
BENCHMARK(BM_Real_Socket_Ingress)->Unit(benchmark::kNanosecond);

static void BM_Real_Socket_Egress(benchmark::State& state) {
    using NetBus = core::spsc_conduit<transport::raw_tcp_frame*, 1024>;
    using Pool = core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>;
    NetBus out_bus;
    Pool pool;
    int tx_fd, rx_fd;
    setup_loopback(tx_fd, rx_fd);

    std::atomic<bool> running{true};
    std::thread traffic_sink([&]() {
        char buf[8192];
        while (running.load(std::memory_order_relaxed)) {
            while(::recv(rx_fd, buf, sizeof(buf), 0) > 0) {}
            _mm_pause();
        }
    });

    io::socket_egress<transport::raw_tcp_frame, NetBus, Pool> egress(tx_fd, out_bus, pool);

    for (auto _ : state) {
        auto* frame = pool.make_raw();
        if (frame) {
            frame->payload_length = 512;
            out_bus.push(frame);
        }
        egress.poll();
    }

    running = false;
    traffic_sink.join();
    close(tx_fd); close(rx_fd);
    state.SetBytesProcessed(state.iterations() * 512);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_Real_Socket_Egress)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();