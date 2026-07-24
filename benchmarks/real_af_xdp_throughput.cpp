/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 */

#include <benchmark/benchmark.h>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <unistd.h>
#include <immintrin.h>
#include <iostream>

#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/io/uring_ingress_xdp.hpp"
#include "slabflux/io/uring_egress_xdp.hpp"
#include "slabflux/io/uring_duplex_xdp.hpp"
#include "slabflux/core/mpmc_pool.hpp"
#include "slabflux/transport/wire_protocol.hpp"

using namespace slabflux;
using namespace slabflux::io;

struct real_xdp_logic {
    std::size_t frames_received{0};
    
    bool on_raw_frame(const void* frame, size_t len) noexcept { 
        frames_received++;
        benchmark::DoNotOptimize(frame);
        return true; 
    }
    
    void on_vector_batch(void** frames, size_t count) noexcept {
        frames_received += count;
        benchmark::DoNotOptimize(frames);
    }
};

static void setup_veth() {
    // Create isolated veth pair for testing
    std::system("ip link add veth_rx type veth peer name veth_tx 2>/dev/null");
    std::system("ip link set veth_rx up 2>/dev/null");
    std::system("ip link set veth_tx up 2>/dev/null");
    
    // Critical for AF_XDP: Promisc mode and disable hardware offloads
    std::system("ip link set veth_rx promisc on 2>/dev/null");
    std::system("ip link set veth_tx promisc on 2>/dev/null");
    std::system("ethtool -K veth_rx rx off tx off 2>/dev/null");
    std::system("ethtool -K veth_tx rx off tx off 2>/dev/null");
}

struct xsk_driver {
    ::xsk_socket* xsk{nullptr};
    ::xsk_umem* umem{nullptr};
    ::xsk_ring_prod fill{};
    ::xsk_ring_cons comp{};
    ::xsk_ring_cons rx{};
    ::xsk_ring_prod tx{};

    bool bind(const char* ifname, void* buffer_area, size_t buffer_size) {
        xdp_shim::umem_params umem_p{2048, 2048, 4096, 0, 0};
        if (xdp_shim::umem_create(&umem, buffer_area, buffer_size, &fill, &comp, umem_p) < 0) return false;
        xdp_shim::socket_params sock_p{2048, 2048, 0, 0};
        if (xdp_shim::socket_create(&xsk, ifname, 0, umem, &rx, &tx, sock_p) < 0) return false;
        return true;
    }
};

static void teardown_veth() {
    std::system("ip link del veth_rx 2>/dev/null");
}

static void BM_Real_AF_XDP_Ingress(benchmark::State& state) {
    if (geteuid() != 0) {
        state.SkipWithError("Real AF_XDP benchmark requires root (sudo) privileges.");
        return;
    }

    setup_veth();

    real_xdp_logic logic;
    using FrameType = transport::raw_tcp_frame;
    
    // Pinned memory pool providing physical HugePages to the NIC
    core::mpmc_pool<FrameType, 4096> pool;
    
    uring_ingress_xdp<FrameType, 4096, real_xdp_logic, io_backend::af_xdp_bypass> ingress(logic);

    // Bind directly to the kernel network driver
    if (!ingress.bind_to_nic("veth_rx", 0, pool)) {
        teardown_veth();
        state.SkipWithError("Failed to bind AF_XDP to veth_rx. Ensure libbpf is loaded and kernel supports XDP.");
        return;
    }

    std::atomic<bool> running{true};
    
    // Background thread: Traffic Generator (Pushing raw L2 packets into veth_tx)
    std::thread traffic_generator([&running]() {
        int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (sock < 0) return;

        struct ifreq ifr;
        std::strncpy(ifr.ifr_name, "veth_tx", IFNAMSIZ);
        ioctl(sock, SIOCGIFINDEX, &ifr);

        struct sockaddr_ll saddr{};
        saddr.sll_family = AF_PACKET;
        saddr.sll_ifindex = ifr.ifr_ifindex;
        saddr.sll_halen = ETH_ALEN;
        std::memset(saddr.sll_addr, 0xFF, ETH_ALEN); // Broadcast MAC

        // Minimal 64-byte payload to satisfy Ethernet frame minimums
        char dummy_packet[64] = {0}; 
        
        while (running.load(std::memory_order_relaxed)) {
            for(int i = 0; i < 32; ++i) {
                sendto(sock, dummy_packet, sizeof(dummy_packet), 0, (struct sockaddr*)&saddr, sizeof(saddr));
            }
            _mm_pause();
        }
        close(sock);
    });

    uint64_t actual_items = 0;

    for (auto _ : state) {
        logic.frames_received = 0;
        
        // Poll the physical AF_XDP sockets
        ingress.poll(pool);
        
        if (logic.frames_received > 0) {
            actual_items += logic.frames_received;
        } else {
            state.PauseTiming(); // Pause timer while waiting for the OS to route the packet
            _mm_pause();
            state.ResumeTiming();
        }
    }

    running = false;
    traffic_generator.join();
    teardown_veth();

    state.SetBytesProcessed(actual_items * 64);
    state.SetItemsProcessed(actual_items);
}

BENCHMARK(BM_Real_AF_XDP_Ingress)->Unit(benchmark::kNanosecond);

static void BM_Real_AF_XDP_Egress(benchmark::State& state) {
    if (geteuid() != 0) return;
    setup_veth();

    using FrameType = transport::raw_tcp_frame;
    using Conduit = core::spsc_conduit<core::tagged_pointer, 4096>;
    using Pool = core::pinned_allocator_spsc<FrameType, 4096>;

    Conduit conduit;
    Pool pool;
    xsk_driver driver;

    if (!driver.bind("veth_rx", pool.get_raw_ptr(), pool.get_raw_ptr_size())) {
        teardown_veth();
        state.SkipWithError("Failed to bind AF_XDP to veth_rx.");
        return;
    }

    std::atomic<bool> running{true};
    uring_egress_xdp<Conduit, Pool, 1024> egress(driver.xsk, driver.tx, driver.comp, conduit, pool, running);

    // Background thread: Traffic Sink (Draining veth_tx to prevent kernel queue saturation)
    std::thread traffic_sink([&running]() {
        int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (sock < 0) return;

        struct ifreq ifr;
        std::strncpy(ifr.ifr_name, "veth_tx", IFNAMSIZ);
        ioctl(sock, SIOCGIFINDEX, &ifr);

        struct sockaddr_ll saddr{};
        saddr.sll_family = AF_PACKET;
        saddr.sll_ifindex = ifr.ifr_ifindex;
        saddr.sll_protocol = htons(ETH_P_ALL);
        bind(sock, (struct sockaddr*)&saddr, sizeof(saddr));

        char dummy_packet[2048]; 
        while (running.load(std::memory_order_relaxed)) {
            while (recvfrom(sock, dummy_packet, sizeof(dummy_packet), MSG_DONTWAIT, nullptr, nullptr) > 0) {}
            _mm_pause();
        }
        close(sock);
    });

    uint64_t actual_items = 0;

    for (auto _ : state) {
        FrameType* alloc_batch[32];
        size_t n = pool.make_batch(alloc_batch, 32);
        if (n > 0) {
            core::tagged_pointer tokens[32];
            for (size_t i = 0; i < n; ++i) {
                alloc_batch[i]->payload_length = 64;
                tokens[i] = core::tagged_pointer::pack(1, alloc_batch[i]);
            }
            size_t pushed = conduit.push_batch(tokens, n);
            if (pushed < n) pool.release_batch(alloc_batch + pushed, n - pushed);
            actual_items += pushed;
        }
        
        egress.poll_egress();
    }

    running = false;
    traffic_sink.join();
    teardown_veth();

    state.SetBytesProcessed(actual_items * 64);
    state.SetItemsProcessed(actual_items);
}
BENCHMARK(BM_Real_AF_XDP_Egress)->Unit(benchmark::kNanosecond);

static void BM_Real_AF_XDP_Duplex(benchmark::State& state) {
    if (geteuid() != 0) return;
    setup_veth();

    using FrameType = transport::raw_tcp_frame;
    using Conduit = core::spsc_conduit<core::tagged_pointer, 4096>;
    using Pool = core::pinned_allocator_spsc<FrameType, 4096>;

    Conduit in_conduit, out_conduit;
    Pool pool;
    xsk_driver driver;

    if (!driver.bind("veth_rx", pool.get_raw_ptr(), pool.get_raw_ptr_size())) {
        teardown_veth();
        state.SkipWithError("Failed to bind AF_XDP to veth_rx.");
        return;
    }

    // Pre-fill the NIC RX ring with buffers so it can receive
    for (int i = 0; i < 1024; ++i) {
        uint32_t idx;
        if (xdp_shim::fill_reserve(&driver.fill, 1, &idx) == 1) {
            *xdp_shim::fill_addr(&driver.fill, idx) = i * 4096;
            xdp_shim::fill_submit(&driver.fill, 1);
        }
    }

    std::atomic<bool> running{true};
    uring_duplex_xdp<Conduit, Conduit, Pool, 1024> duplex(
        driver.xsk, driver.fill, driver.comp, driver.rx, driver.tx, 
        in_conduit, out_conduit, pool, running
    );

    std::thread traffic_echo([&running]() {
        int sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
        if (sock < 0) return;
        struct ifreq ifr;
        std::strncpy(ifr.ifr_name, "veth_tx", IFNAMSIZ);
        ioctl(sock, SIOCGIFINDEX, &ifr);
        struct sockaddr_ll saddr{};
        saddr.sll_family = AF_PACKET;
        saddr.sll_ifindex = ifr.ifr_ifindex;
        saddr.sll_protocol = htons(ETH_P_ALL);
        bind(sock, (struct sockaddr*)&saddr, sizeof(saddr));

        char buffer[2048]; 
        while (running.load(std::memory_order_relaxed)) {
            // Receive from XDP Egress, send back to XDP Ingress
            int len = recvfrom(sock, buffer, sizeof(buffer), MSG_DONTWAIT, nullptr, nullptr);
            if (len > 0) { sendto(sock, buffer, len, 0, (struct sockaddr*)&saddr, sizeof(saddr)); }
            else _mm_pause();
        }
        close(sock);
    });

    uint64_t actual_items = 0;
    for (auto _ : state) {
        duplex.poll_runtime();
        core::tagged_pointer tokens[32];
        actual_items += in_conduit.pop_batch(tokens, 32);
    }

    running = false;
    traffic_echo.join();
    teardown_veth();

    state.SetBytesProcessed(actual_items * 64);
    state.SetItemsProcessed(actual_items);
}
BENCHMARK(BM_Real_AF_XDP_Duplex)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();