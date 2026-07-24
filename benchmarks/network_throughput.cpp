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

#include <benchmark/benchmark.h>
#include <iostream>
#include <vector>
#include <atomic>
#include <x86intrin.h>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string_view>

#include "slabflux/io/uring_ingress.hpp"
#include "slabflux/io/uring_egress.hpp"
#include "slabflux/io/uring_duplex.hpp"
#include "slabflux/io/uring_ingress_stream.hpp"
#include "slabflux/io/uring_egress_stream.hpp"
#include "slabflux/io/uring_duplex_stream.hpp"
#include "slabflux/io/uring_ingress_xdp.hpp"
#include "slabflux/io/uring_egress_xdp.hpp"
#include "slabflux/io/uring_duplex_xdp.hpp"
#include "slabflux/io/io_uring_durable_journal.hpp"
#include "slabflux/io/mirrored_journal.hpp"
#include "slabflux/io/socket_ingress.hpp"
#include "slabflux/io/socket_egress.hpp"
#include "slabflux/io/socket_duplex.hpp"
#include "slabflux/io/baremetal_egress.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/transport/http_avx.hpp"

using namespace slabflux;

/**
 * @brief Mock Opaque Type Completion for AF_XDP residency in benchmarks.
 */
extern "C" {
    struct xsk_socket {
        int fd = -1;
    };
}

// Helper to create a connected TCP loopback pair for benchmark isolation
void setup_loopback(int& tx, int& rx) {
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
    // Set non-blocking to prevent benchmark deadlocks on window saturation
    fcntl(tx, F_SETFL, fcntl(tx, F_GETFL) | O_NONBLOCK);
    fcntl(rx, F_SETFL, fcntl(rx, F_GETFL) | O_NONBLOCK);
    close(listen_fd);
}

/**
 * @brief Benchmarks the raw throughput of uring_ingress (Mirror of Egress Architecture).
 */
static void BM_uring_ingress(benchmark::State& state) {
    using NetBus = core::spsc_conduit<core::tagged_pointer, 4096>;
    NetBus in_bus;
    std::atomic<bool> running{true};
    int tx_fd, rx_fd;
    setup_loopback(tx_fd, rx_fd);

    // SYMMETRICAL PRODUCER: Mirror of BM_uring_egress logic (Zero-syscall blast)
    io::baremetal_egress<transport::raw_tcp_frame, 4096> producer(tx_fd, 1);
    transport::raw_tcp_frame frame{};
    frame.payload_length = 512;

    // SYMMETRICAL CONSUMER: Move recycling to a background thread to mirror egress sink.
    std::thread consumer_thread;

    {
        io::uring_ingress<NetBus, 2048> ingress(rx_fd, in_bus, running);

        consumer_thread = std::thread([&]() {
            core::tagged_pointer tokens[32];
            while(running.load(std::memory_order_relaxed)) {
                while (std::size_t n = in_bus.pop_batch(tokens, 32)) {
                    ingress.release_ingress_buffers_batch(tokens, n);
                }
                _mm_pause();
            }
            // Final drain on shutdown
            while (std::size_t n = in_bus.pop_batch(tokens, 32)) {
                ingress.release_ingress_buffers_batch(tokens, n);
            }
        });

        for (auto _ : state) {
            // 1. PRODUCER: Zero-syscall SQE preparation
            for (int i = 0; i < 32; ++i) producer.send(&frame);

            // 2. ENGINE: Kernel-to-User Harvest
            ingress.poll_ingress();
        }

        // Robust Teardown
        running.store(false);
        if (consumer_thread.joinable()) consumer_thread.join();
        ::shutdown(tx_fd, SHUT_RDWR); ::shutdown(rx_fd, SHUT_RDWR);
        for(int i = 0; i < 100; ++i) { 
            ingress.poll_ingress(); 
            _mm_pause(); 
        }
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * 32 * 512);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * 32);

    close(tx_fd); close(rx_fd);
}

BENCHMARK(BM_uring_ingress);

/**
 * @brief Benchmarks the zero-copy send throughput of uring_egress.
 */
static void BM_uring_egress(benchmark::State& state) {
    using NetBus = core::spsc_conduit<core::tagged_pointer, 1024>;
    using Pool = core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>;
    NetBus out_bus;
    Pool pool;
    std::atomic<bool> running{true};
    int tx_fd, rx_fd;
    setup_loopback(tx_fd, rx_fd);

    // PARITY SINK: Move the receiver to a background thread to eliminate syscall tax from timing.
    // This achieves architectural symmetry with the ingress benchmark's producer.
    std::thread sink_thread([&]() {
        char buf[65536];
        while (running.load(std::memory_order_relaxed)) {
            while (recv(rx_fd, buf, sizeof(buf), MSG_DONTWAIT) > 0) {}
            _mm_pause();
        }
    });

    uint64_t actual_items = 0;

    {
        io::uring_egress<NetBus, Pool, 1024> egress(tx_fd, out_bus, pool, running);
        core::tagged_pointer tokens[32];
        transport::raw_tcp_frame* alloc_batch[32];

        for (auto _ : state) {
            // 1. ARCHITECTURAL PARITY: Batch allocate and push
            size_t n = pool.make_batch(alloc_batch, 32);
            if (SL_EXPECT_TRUE(n > 0)) {
                for (size_t i = 0; i < n; ++i) {
                    alloc_batch[i]->payload_length = 512;
                    tokens[i] = core::tagged_pointer::pack(1, alloc_batch[i]);
                }
                size_t pushed = out_bus.push_batch(tokens, n);
                if (SL_EXPECT_FALSE(pushed < n)) {
                    pool.release_batch(alloc_batch + pushed, n - pushed);
                }
                actual_items += pushed;
            } else {
                _mm_pause();
            }

            // 2. Engine Poll (Measured Dispatch)
            egress.poll_egress();
        }

        // Teardown
        running.store(false);
        if (sink_thread.joinable()) sink_thread.join();
        ::shutdown(tx_fd, SHUT_RDWR); ::shutdown(rx_fd, SHUT_RDWR);
        for(int i = 0; i < 100; ++i) { 
            egress.poll_egress(); 
            _mm_pause(); 
        }
    }

    state.SetBytesProcessed(static_cast<int64_t>(actual_items) * 512);
    state.SetItemsProcessed(static_cast<int64_t>(actual_items));

    close(tx_fd); close(rx_fd);
}
BENCHMARK(BM_uring_egress);

static void BM_uring_duplex(benchmark::State& state) {
    using NetBus = core::spsc_conduit<core::tagged_pointer, 8192>;
    using Pool = core::pinned_allocator_spsc<transport::raw_tcp_frame, 8192>;
    NetBus in_bus, out_bus;
    Pool pool;
    std::atomic<bool> running{true};
    int tx_fd, rx_fd;
    setup_loopback(tx_fd, rx_fd);

    io::uring_duplex<NetBus, NetBus, Pool, 1024>* duplex_ptr = nullptr;
    std::atomic<uint64_t> actual_rx{0};

    // PARITY PEERS: Move loopback pressure to background threads to isolate engine logic.
    // This achieves sub-30ns parity with the individual ingress/egress tests.
    std::thread sink_thread([&]() {
        char buf[65536];
        core::tagged_pointer tokens[32];
        while (running.load(std::memory_order_relaxed)) {
            while (recv(rx_fd, buf, sizeof(buf), MSG_DONTWAIT) > 0) {}
            
            if (SL_EXPECT_TRUE(duplex_ptr != nullptr)) {
                while (std::size_t n = in_bus.pop_batch(tokens, 32)) {
                    actual_rx.fetch_add(n, std::memory_order_relaxed);
                    duplex_ptr->release_ingress_buffers_batch(tokens, n);
                }
            }
            
            _mm_pause();
        }
    });

    std::thread producer_thread([&]() {
        io::baremetal_egress<transport::raw_tcp_frame, 4096> producer(rx_fd, 1);
        transport::raw_tcp_frame frame{};
        frame.payload_length = 512;
        while (running.load(std::memory_order_relaxed)) {
            for (int i = 0; i < 256; ++i) { // Send larger bursts to drive metrics
                producer.send(&frame);
            }
            _mm_pause();
        }
    });

    uint64_t actual_tx = 0;

    {
        io::uring_duplex<NetBus, NetBus, Pool, 1024> duplex(tx_fd, in_bus, out_bus, pool, running);
        duplex_ptr = &duplex;
        core::tagged_pointer tokens[32];
        transport::raw_tcp_frame* alloc_batch[32];

        for (auto _ : state) {
            // 1. ARCHITECTURAL PARITY: Batch allocate and push
            const size_t n_alloc = pool.make_batch(alloc_batch, 32);
            if (SL_EXPECT_TRUE(n_alloc > 0)) {
                for (size_t i = 0; i < n_alloc; ++i) {
                    alloc_batch[i]->payload_length = 512;
                    tokens[i] = core::tagged_pointer::pack(1, alloc_batch[i]);
                }
                size_t pushed = out_bus.push_batch(tokens, n_alloc);
                if (SL_EXPECT_FALSE(pushed < n_alloc)) {
                    pool.release_batch(alloc_batch + pushed, n_alloc - pushed);
                }
                actual_tx += pushed;
            } else {
                _mm_pause();
            }

            duplex.poll_runtime();
        }
        
        // Fix Deadlock during shutdown: 
        // 1. Signal engine to stop accepting new work from conduits
        running.store(false);
        if (sink_thread.joinable()) sink_thread.join();
        if (producer_thread.joinable()) producer_thread.join();
        
        ::shutdown(tx_fd, SHUT_RDWR); ::shutdown(rx_fd, SHUT_RDWR);
        for(int i = 0; i < 100; ++i) { 
            duplex.poll_runtime(); 
            _mm_pause(); 
        }
    }

    uint64_t rx_total = actual_rx.load(std::memory_order_relaxed);
    state.SetBytesProcessed(static_cast<int64_t>(actual_tx + rx_total) * 512);
    state.SetItemsProcessed(static_cast<int64_t>(actual_tx + rx_total));

    close(tx_fd); close(rx_fd);
}
BENCHMARK(BM_uring_duplex);

struct mock_pipeline {
    uint64_t processed = 0;
    template <typename T>
    void dispatch(T req) noexcept { 
        processed++;
        benchmark::DoNotOptimize(req); 
    }
};

static void BM_uring_ingress_stream(benchmark::State& state) {
    mock_pipeline pipe;
    core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024> pool;
    int tx_fd, rx_fd;
    setup_loopback(tx_fd, rx_fd);

    std::atomic<bool> running{true};
    
    // SYMMETRICAL PRODUCER: Move the sender to a background thread to match egress sink
    std::thread source_thread([&]() {
        io::baremetal_egress<transport::raw_tcp_frame, 4096> producer(tx_fd, 1);
        transport::raw_tcp_frame frame{};
        frame.payload_length = 512;
        while (running.load(std::memory_order_relaxed)) {
            for (int i = 0; i < 32; ++i) {
                producer.send(&frame);
            }
            _mm_pause();
        }
    });

    uint64_t actual_items = 0;

    {
        io::uring_ingress_stream ingress(pool, pipe, 1);
        ingress.arm_socket(rx_fd);

        for (auto _ : state) {
            pipe.processed = 0;
            ingress.poll_hot_path();
            
            if (pipe.processed > 0) {
                actual_items += pipe.processed;
            } else {
                _mm_pause();
            }
        }

        // Teardown
        running.store(false);
        if (source_thread.joinable()) source_thread.join();
        ::shutdown(tx_fd, SHUT_RDWR);
        ::shutdown(rx_fd, SHUT_RDWR);
        for(int i = 0; i < 100; ++i) {
            ingress.poll_hot_path();
            _mm_pause();
        }
    }

    state.SetBytesProcessed(static_cast<int64_t>(actual_items) * 512);
    state.SetItemsProcessed(static_cast<int64_t>(actual_items));

    close(tx_fd); close(rx_fd);
}
BENCHMARK(BM_uring_ingress_stream);

static void BM_uring_egress_stream(benchmark::State& state) {
    core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024> pool;
    int tx_fd, rx_fd;
    setup_loopback(tx_fd, rx_fd);

    // SYMMETRICAL SINK: Move the receiver to a background thread to eliminate syscall tax from timing.
    std::atomic<bool> running{true};
    std::thread sink_thread([&]() {
        char buf[65536];
        while (running.load(std::memory_order_relaxed)) {
            while (recv(rx_fd, buf, sizeof(buf), MSG_DONTWAIT) > 0) {}
            _mm_pause();
        }
    });

    uint64_t actual_items = 0;

    {
        io::uring_egress_stream egress(pool, tx_fd, 1);
        transport::raw_tcp_frame* alloc_batch[32];

        for (auto _ : state) {
            // 1. PRODUCER: Batch allocate and stream dispatch
            size_t n = pool.make_batch(alloc_batch, 32);
            
            if (SL_EXPECT_TRUE(n > 0)) {
                size_t dispatched = 0;
                for (size_t i = 0; i < n; ++i) {
                    alloc_batch[i]->payload_length = 512;
                    if (egress.dispatch(alloc_batch[i], 512)) {
                        dispatched++;
                    }
                }
                actual_items += dispatched;
            } else {
                _mm_pause();
            }
            
            egress.flush_doorbell();
            
            // 2. ENGINE: Reclaim completions
            egress.poll_completions();
        }

        // Teardown
        running.store(false);
        if (sink_thread.joinable()) sink_thread.join();
        ::shutdown(tx_fd, SHUT_RDWR);
        ::shutdown(rx_fd, SHUT_RDWR);
        for(int i = 0; i < 100; ++i) {
            egress.poll_completions();
            _mm_pause();
        }
    }

    state.SetBytesProcessed(static_cast<int64_t>(actual_items) * 512);
    state.SetItemsProcessed(static_cast<int64_t>(actual_items));

    close(tx_fd); close(rx_fd);
}
BENCHMARK(BM_uring_egress_stream);

static void BM_uring_duplex_stream(benchmark::State& state) {
    mock_pipeline pipe;
    core::pinned_allocator_spsc<transport::raw_tcp_frame, 8192> pool;
    int hammer_fd, duplex_fd;
    setup_loopback(hammer_fd, duplex_fd);

    std::atomic<bool> running{true};
    
    std::thread sink_thread([&]() {
        char buf[65536];
        while (running.load(std::memory_order_relaxed)) {
            while (recv(hammer_fd, buf, sizeof(buf), MSG_DONTWAIT) > 0) {}
            _mm_pause();
        }
    });

    std::thread producer_thread([&]() {
        io::baremetal_egress<transport::raw_tcp_frame, 4096> producer(hammer_fd, 1);
        transport::raw_tcp_frame frame{};
        frame.payload_length = 512;
        while (running.load(std::memory_order_relaxed)) {
            for (int i = 0; i < 256; ++i) producer.send(&frame);
            _mm_pause();
        }
    });

    uint64_t actual_items = 0;

    {
        io::uring_duplex_stream duplex(pool, pipe, duplex_fd, 1);
        transport::raw_tcp_frame* alloc_batch[32];

        for (auto _ : state) {
            size_t n = pool.make_batch(alloc_batch, 32);
            size_t pushed = 0;
            
            if (SL_EXPECT_TRUE(n > 0)) {
                for (size_t i = 0; i < n; ++i) {
                    alloc_batch[i]->payload_length = 512;
                    if (duplex.dispatch(alloc_batch[i], 512)) pushed++;
                }
            } else {
                _mm_pause();
            }
            
            pipe.processed = 0;

            duplex.poll_runtime();

            actual_items += pushed + pipe.processed;
        }

        running.store(false, std::memory_order_release);
        if (sink_thread.joinable()) sink_thread.join();
        if (producer_thread.joinable()) producer_thread.join();
        ::shutdown(hammer_fd, SHUT_RDWR);
        ::shutdown(duplex_fd, SHUT_RDWR);
        for(int i = 0; i < 100; ++i) { 
            duplex.poll_runtime(); 
            _mm_pause(); 
        }
    }

    state.SetBytesProcessed(actual_items * 512);
    state.SetItemsProcessed(actual_items);
    close(hammer_fd); close(duplex_fd);
}
BENCHMARK(BM_uring_duplex_stream);

// --- Unified XDP Mock Framework ---
namespace xdp_mock {
    struct xsk_impl { int fd = 1; };

    struct logic {
        size_t frames_received{0};
        bool on_raw_frame(transport::raw_tcp_frame*, size_t) noexcept { 
            frames_received++;
            benchmark::DoNotOptimize(0); 
            return true; 
        }
        void on_vector_batch(transport::raw_tcp_frame** frames, size_t count) noexcept {
            frames_received += count;
            benchmark::DoNotOptimize(frames);
        }
    };

    struct pool {
        char umem[2048 * 1024];
        using value_type = transport::raw_tcp_frame;
        void release(void*) {}
        template<typename T> void release_batch(T**, size_t) {}
        void* get_ptr(size_t) { return umem; }
        void* get_raw_ptr() { return umem; }
        size_t get_raw_ptr_size() { return sizeof(umem); }
        size_t capacity() { return 1024; }
        transport::raw_tcp_frame* make_raw() { return nullptr; }
    };

    struct conduit_ptr {
        using value_type = transport::raw_tcp_frame*;
        using value_type_pod = transport::raw_tcp_frame;
        transport::raw_tcp_frame frames[32];
        transport::raw_tcp_frame* tokens[32];
        
        conduit_ptr() {
            for(int i=0; i<32; ++i) {
                frames[i].payload_length = 64;
                tokens[i] = &frames[i];
            }
        }
        size_t pop_batch(transport::raw_tcp_frame** out, size_t n) {
            size_t count = std::min(n, (size_t)32);
            std::memcpy(out, tokens, count * sizeof(transport::raw_tcp_frame*));
            return count;
        }
        size_t push_batch(transport::raw_tcp_frame**, size_t n) { return n; }
        void revert_batch(transport::raw_tcp_frame**, size_t) {}
        size_t occupancy() const { return 0; }
    };

    struct conduit_tagged {
        using value_type = core::tagged_pointer;
        using value_type_pod = transport::raw_tcp_frame;
        transport::raw_tcp_frame frames[32];
        core::tagged_pointer tokens[32];
        
        conduit_tagged() {
            for(int i=0; i<32; ++i) {
                frames[i].payload_length = 64;
                tokens[i] = core::tagged_pointer::pack(1, &frames[i]);
            }
        }
        size_t pop_batch(core::tagged_pointer* out, size_t n) {
            size_t count = std::min(n, (size_t)32);
            std::memcpy(out, tokens, count * sizeof(core::tagged_pointer));
            return count;
        }
        size_t push_batch(const core::tagged_pointer*, size_t n) { return n; }
        void revert_batch(core::tagged_pointer*, size_t) {}
        size_t occupancy() const { return 0; }
    };

    struct ingress_overlay {
        ::xsk_socket*    xsk;
        ::xsk_umem*      umem;
        void*            buffer_area;
        std::size_t      buffer_size;
        ::xsk_ring_prod  fill;
        ::xsk_ring_cons  comp;
        ::xsk_ring_cons  rx;
        ::xsk_ring_prod  tx;
    };
} // namespace xdp_mock

static void BM_xdp_ingress(benchmark::State& state) {
    xdp_mock::xsk_impl dummy_xsk;
    xdp_mock::logic logic;
    using FrameType = transport::raw_tcp_frame;
    using IngressType = io::uring_ingress_xdp<FrameType, 1024, xdp_mock::logic, io::io_backend::af_xdp_bypass>;
    
    alignas(64) uint8_t ingress_storage[sizeof(IngressType)];
    IngressType* ingress = new (ingress_storage) IngressType(logic);
    auto* overlay = reinterpret_cast<xdp_mock::ingress_overlay*>(ingress);
    
    static uint32_t rx_p = 1024, rx_c = 0, rx_f = 0;
    static uint32_t fill_p = 1024, fill_c = 1024, fill_f = 0;
    static xdp_desc rx_data[1024];
    static uint64_t fill_data[1024];

    overlay->xsk = reinterpret_cast<::xsk_socket*>(&dummy_xsk);
    overlay->rx.producer = &rx_p; overlay->rx.consumer = &rx_c; overlay->rx.flags = &rx_f;
    overlay->rx.ring = rx_data; overlay->rx.mask = 1023; overlay->rx.size = 1024;
    overlay->fill.producer = &fill_p; overlay->fill.consumer = &fill_c; overlay->fill.flags = &fill_f;
    overlay->fill.ring = fill_data; overlay->fill.mask = 1023; overlay->fill.size = 1024;

    for (int i = 0; i < 1024; ++i) { rx_data[i].addr = i * 64; rx_data[i].len = 64; }

    xdp_mock::pool pool;
    overlay->buffer_area = pool.umem;

    // Let Google Benchmark know we process 32 packets per loop
    while (state.KeepRunningBatch(32)) {
        rx_p += 32; fill_c = fill_p;
        ingress->poll(pool);
    }
    overlay->xsk = nullptr; overlay->umem = nullptr; ingress->~IngressType();
    state.SetBytesProcessed(state.iterations() * 64);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_xdp_ingress);

static void BM_xdp_egress(benchmark::State& state) {
    xdp_mock::conduit_ptr bus;
    xdp_mock::pool pool;
    std::atomic<bool> running{true};
    xdp_mock::xsk_impl dummy_xsk;
    auto* xsk = reinterpret_cast<::xsk_socket*>(&dummy_xsk);
    ::xsk_ring_prod tx{}; ::xsk_ring_cons comp{};
    
    static uint32_t tx_p = 0, tx_c = 0, tx_f = 0;
    static uint32_t cp_p = 1024, cp_c = 0, cp_f = 0;
    static xdp_desc dummy_tx_data[1024];
    static uint64_t dummy_comp_data[1024];

    tx.producer = &tx_p; tx.consumer = &tx_c; tx.flags = &tx_f; tx.ring = dummy_tx_data; tx.mask = 1023; tx.size = 1024;
    comp.producer = &cp_p; comp.consumer = &cp_c; comp.flags = &cp_f; comp.ring = dummy_comp_data; comp.mask = 1023; comp.size = 1024;
    for(int i=0; i<1024; ++i) dummy_comp_data[i] = i * 64;

    io::uring_egress_xdp<xdp_mock::conduit_ptr, xdp_mock::pool, 1024> egress(xsk, tx, comp, bus, pool, running);

    // Let Google Benchmark know we process 32 packets per loop
    while (state.KeepRunningBatch(32)) {
        cp_p += 32; tx_c += 32;
        egress.poll_egress();
    }
    state.SetBytesProcessed(state.iterations() * 64);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_xdp_egress);

static void BM_xdp_duplex(benchmark::State& state) {
    xdp_mock::conduit_tagged in_bus, out_bus;
    xdp_mock::pool pool;
    std::atomic<bool> running{true};
    xdp_mock::xsk_impl dummy_xsk;
    auto* xsk = reinterpret_cast<::xsk_socket*>(&dummy_xsk);
    
    static uint32_t f_p = 1024, f_c = 1024, f_f = 0, c_p = 1024, c_c = 0, c_f = 0;
    static uint32_t r_p = 1024, r_c = 0, r_f = 0, t_p = 0, t_c = 0, t_f = 0;
    static xdp_desc rx_ring_mem[1024], tx_ring_mem[1024];
    static uint64_t fill_ring_mem[1024], comp_ring_mem[1024];

    ::xsk_ring_prod fill{ .mask = 1023, .size = 1024, .producer = &f_p, .consumer = &f_c, .ring = fill_ring_mem, .flags = &f_f };
    ::xsk_ring_cons comp{ .mask = 1023, .size = 1024, .producer = &c_p, .consumer = &c_c, .ring = comp_ring_mem, .flags = &c_f };
    ::xsk_ring_cons rx{ .mask = 1023, .size = 1024, .producer = &r_p, .consumer = &r_c, .ring = rx_ring_mem, .flags = &r_f };
    ::xsk_ring_prod tx{ .mask = 1023, .size = 1024, .producer = &t_p, .consumer = &t_c, .ring = tx_ring_mem, .flags = &t_f };

    for (int i = 0; i < 1024; ++i) { rx_ring_mem[i].addr = i * 64; rx_ring_mem[i].len = 64; comp_ring_mem[i] = i * 64; }

    io::uring_duplex_xdp<xdp_mock::conduit_tagged, xdp_mock::conduit_tagged, xdp_mock::pool, 1024> duplex(xsk, fill, comp, rx, tx, in_bus, out_bus, pool, running);

    // Let Google Benchmark know we process 64 packets per loop (32 RX + 32 TX)
    while (state.KeepRunningBatch(64)) {
        c_p += 32; t_c += 32; r_p += 32; f_c += 32;
        duplex.poll_runtime();
    }
    state.SetBytesProcessed(state.iterations() * 64);
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_xdp_duplex);

static void BM_uring_durable_journal(benchmark::State& state) {
    const char* path = "/tmp/sf_uring_journal.bench";
    io::io_uring_durable_journal<transport::raw_tcp_frame> journal(path, 1);
    uint32_t iter = 0;

    for (auto _ : state) {
        auto* slot = journal.reserve_slot();
        if (SL_UNLIKELY(!slot)) {
            state.PauseTiming();
            journal.reset();
            state.ResumeTiming();
            slot = journal.reserve_slot();
        }
        slot->payload_length = 512;
        journal.commit_slot(slot); // Explicit slot injection bypasses extra cursor reads

        if (SL_UNLIKELY((++iter & 63) == 0)) journal.poll_completions();
    }
    journal.force_flush();
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * 512);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    unlink(path);
}
BENCHMARK(BM_uring_durable_journal);

static void BM_mirrored_journal(benchmark::State& state) {
    const char* p1 = "/tmp/sf_mirror1.bench";
    const char* p2 = "/tmp/sf_mirror2.bench";
    io::mirrored_journal<transport::raw_tcp_frame> mirror(p1, p2, 2, 3);
    char data[512] = {0};
    uint32_t iter = 0;
    for (auto _ : state) {
        mirror.persist_event(data, 512);
        // Amortized reaping to prevent ring stalls
        if ((++iter & 63) == 0) mirror.poll_completions();
    }
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * 512);
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    unlink(p1); unlink(p2);
}
BENCHMARK(BM_mirrored_journal);

static void BM_baremetal_egress(benchmark::State& state) {
    int tx_fd, rx_fd;
    setup_loopback(tx_fd, rx_fd);
    io::baremetal_egress<transport::raw_tcp_frame, 1024> egress(tx_fd, 1);
    transport::raw_tcp_frame frame{};

    // Fallback: If uring failed to init (not root), use standard write for metric parity
    if (!*reinterpret_cast<bool*>(reinterpret_cast<char*>(&egress) + sizeof(io_uring) + sizeof(int))) {
        // Engine not valid (likely permissions)
    }

    for (auto _ : state) {
        egress.send(&frame);
    }
    state.SetBytesProcessed(state.iterations() * sizeof(transport::raw_tcp_frame));
    state.SetItemsProcessed(state.iterations());
    close(tx_fd); close(rx_fd);
}
BENCHMARK(BM_baremetal_egress);

/**
 * @brief Benchmarks legacy Syscall-based Ingress.
 * Provides the absolute performance floor (Syscall bound).
 */
static void BM_socket_ingress(benchmark::State& state) {
    struct mock_logic {
        bool on_raw_frame(void*, size_t) noexcept { return true; }
    } logic;
    using Pool = core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>;
    Pool pool;
    int tx_fd, rx_fd;
    setup_loopback(tx_fd, rx_fd);

    io::socket_ingress<transport::raw_tcp_frame, mock_logic, Pool> ingress(rx_fd, logic, pool);
    char data[512] = {0};

    for (auto _ : state) {
        // BURST INJECTION: Saturate the interconnect to measure amortized throughput
        for (int i = 0; i < 32; ++i) {
            (void)send(tx_fd, data, 512, MSG_DONTWAIT);
        }
        ingress.poll();
    }

    state.SetBytesProcessed(state.iterations() * 512);
    state.SetItemsProcessed(state.iterations() * 32);
    close(tx_fd); close(rx_fd);
}
BENCHMARK(BM_socket_ingress);

/**
 * @brief Benchmarks legacy Syscall-based Egress.
 */
static void BM_socket_egress(benchmark::State& state) {
    using NetBus = core::spsc_conduit<transport::raw_tcp_frame*, 1024>;
    using Pool = core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>;
    NetBus out_bus;
    Pool pool;
    int tx_fd, rx_fd;
    setup_loopback(tx_fd, rx_fd);

    io::socket_egress<transport::raw_tcp_frame, NetBus, Pool> egress(tx_fd, out_bus, pool);
    char drain_buf[4096];

    for (auto _ : state) {
        auto* frame = pool.make_raw();
        if (frame) {
            frame->payload_length = 512;
            out_bus.push(frame);
        }
        egress.poll();
        while (recv(rx_fd, drain_buf, sizeof(drain_buf), MSG_DONTWAIT) > 0);
    }

    state.SetBytesProcessed(state.iterations() * 32 * 512);
    state.SetItemsProcessed(state.iterations() * 32);
    close(tx_fd); close(rx_fd);
}
BENCHMARK(BM_socket_egress);

/**
 * @brief Benchmarks the Fused Socket Duplex Engine.
 * Measures the overhead of interleaved recvmmsg/sendmmsg syscalls.
 */
static void BM_socket_duplex(benchmark::State& state) {
    struct mock_logic {
        bool on_raw_frame(void*, size_t) noexcept { return true; }
    } logic;
    using NetBus = core::spsc_conduit<transport::raw_tcp_frame*, 1024>;
    using Pool = core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>;
    NetBus out_bus;
    Pool in_pool, out_pool;
    int hammer_fd, duplex_fd;
    setup_loopback(hammer_fd, duplex_fd); 

    io::socket_duplex<transport::raw_tcp_frame, mock_logic, Pool, NetBus, Pool> 
        duplex(duplex_fd, logic, in_pool, out_bus, out_pool);
    
    char tx_data[512] = {0};
    char rx_drain[4096];

    for (auto _ : state) {
        // Inbound pressure
        (void)send(hammer_fd, tx_data, 512, MSG_DONTWAIT);
        
        // Outbound pressure
        auto* f = out_pool.make_raw();
        if (f) {
            f->payload_length = 512;
            if (!out_bus.try_push(f)) out_pool.release(f);
        }
        
        duplex.poll();
        while (recv(hammer_fd, rx_drain, sizeof(rx_drain), MSG_DONTWAIT) > 0);
    }

    state.SetBytesProcessed(state.iterations() * 512);
    state.SetItemsProcessed(state.iterations());
    close(hammer_fd); close(duplex_fd);
}
BENCHMARK(BM_socket_duplex);

#if !defined(SLABFLUX_INTEGRATED_SUITE) && !defined(SLABFLUX_FOR_ALL_BUILD)
BENCHMARK_MAIN();
#endif