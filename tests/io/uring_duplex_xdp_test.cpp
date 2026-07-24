/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * @file uring_duplex_xdp_test.cpp
 * @brief AF_XDP Unified Duplex Engine Validation Suite.
 */

#include <gtest/gtest.h>
#include <thread>
#include <x86intrin.h>
#include <memory>
#include <chrono>
#include <atomic>
#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/io/uring_duplex_xdp.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/platform/os.hpp"

// Opaque Type Completion for libbpf structures
extern "C" {
    struct xsk_socket { int fd = -1; };
}

using namespace slabflux;

/**
 * @brief Physical Residency Audit.
 * Paradigm Shattering: Validates that the Unified XDP Duplex structure 
 * avoids cache-line splits despite managing 4 independent hardware rings.
 */
TEST(UringDuplexXdpTest, PhysicalResidencyAudit) {
    using NetBus = core::spsc_conduit<core::tagged_pointer, 1024>;
    using NetAlloc = core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>;

    NetBus in_bus, out_bus;
    NetAlloc pool;
    std::atomic<bool> running{true};

    io::uring_duplex_xdp<NetBus, NetBus, NetAlloc, 1024> nexus(
        nullptr, {}, {}, {}, {}, in_bus, out_bus, pool, running
    );

    EXPECT_EQ(reinterpret_cast<uintptr_t>(&nexus) % 64, 0) << "XDP Duplex Engine must be 64-byte aligned.";
    EXPECT_EQ(sizeof(nexus) % 64, 0) << "XDP Duplex Engine size must not bleed across L1 cache sets.";
}

/**
 * @brief Dry-Run Polling Physics.
 * Measures the cost of a full 4-ring scan when no packets are present.
 */
TEST(UringDuplexXdpTest, XdpBypassIdleLatency) {
    using NetBus = core::spsc_conduit<core::tagged_pointer, 1024>;
    using NetAlloc = core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>;

    NetBus in_bus, out_bus;
    NetAlloc pool;
    std::atomic<bool> running{true};

    io::uring_duplex_xdp<NetBus, NetBus, NetAlloc, 1024> nexus(
        nullptr, {}, {}, {}, {}, in_bus, out_bus, pool, running
    );

    constexpr size_t ITERATIONS = 100'000;
    uint64_t start_cycles = __rdtsc();

    for(size_t i = 0; i < ITERATIONS; ++i) {
        nexus.poll_runtime();
    }

    uint64_t end_cycles = __rdtsc();
    double cycles_per_poll = static_cast<double>(end_cycles - start_cycles) / ITERATIONS;

    std::cout << "[PERF] XDP Duplex Idle Latency: " << cycles_per_poll << " cycles/poll\n";
    
    // Requirement: Polling 4 rings must be branchlessly fast (< 25 cycles)
    EXPECT_LT(cycles_per_poll, 25.0);
}

/**
 * @brief Egress Translation Integrity.
 * Validates that packets pulled from the Outbound Conduit are correctly
 * translated into physical UMEM descriptor offsets.
 */
TEST(UringDuplexXdpTest, OutboundDescriptorResolution) {
    using NetBus = core::spsc_conduit<core::tagged_pointer, 1024>;
    using NetAlloc = core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>;

    NetBus in_bus, out_bus;
    NetAlloc pool;
    std::atomic<bool> running{true};

    // Mock XDP structures
    ::xsk_socket mock_xsk;
    ::xsk_ring_prod tx_ring{};
    ::xsk_ring_cons comp_ring{};
    ::xsk_ring_cons rx_ring{};
    ::xsk_ring_prod fill_ring{};
    
    // Mock hardware TX ring array to avoid segmentation faults
    static uint32_t tx_prod = 0, tx_cons = 0, tx_flags = 0;
    static xdp_desc tx_ring_data[1024];
    tx_ring.producer = &tx_prod; tx_ring.consumer = &tx_cons; tx_ring.flags = &tx_flags;
    tx_ring.ring = tx_ring_data; tx_ring.mask = 1023; tx_ring.size = 1024;

    static uint32_t rx_prod = 0, rx_cons = 0, rx_flags = 0;
    static xdp_desc rx_ring_data[1024];
    rx_ring.producer = &rx_prod; rx_ring.consumer = &rx_cons; rx_ring.flags = &rx_flags;
    rx_ring.ring = rx_ring_data; rx_ring.mask = 1023; rx_ring.size = 1024;

    static uint32_t comp_prod = 0, comp_cons = 0, comp_flags = 0;
    static uint64_t comp_ring_data[1024];
    comp_ring.producer = &comp_prod; comp_ring.consumer = &comp_cons; comp_ring.flags = &comp_flags;
    comp_ring.ring = comp_ring_data; comp_ring.mask = 1023; comp_ring.size = 1024;

    static uint32_t fill_prod = 0, fill_cons = 0, fill_flags = 0;
    static uint64_t fill_ring_data[1024];
    fill_ring.producer = &fill_prod; fill_ring.consumer = &fill_cons; fill_ring.flags = &fill_flags;
    fill_ring.ring = fill_ring_data; fill_ring.mask = 1023; fill_ring.size = 1024;

    io::uring_duplex_xdp<NetBus, NetBus, NetAlloc, 1024> nexus(
        &mock_xsk, fill_ring, comp_ring, rx_ring, tx_ring, in_bus, out_bus, pool, running
    );

    // Push 1 frame to outbound conduit
    auto* frame = pool.make_raw();
    ASSERT_NE(frame, nullptr);
    frame->payload_length = 128;
    
    out_bus.push(core::tagged_pointer::pack(1, frame));
    
    // Drive the engine
    nexus.poll_runtime();
    
    // Verify that the frame offset was resolved and written to the XDP TX Ring correctly
    // The ring producer should have advanced by 1
    EXPECT_EQ(tx_prod, 1);
    
    // Calculate expected offset mathematically
    char* umem_base = reinterpret_cast<char*>(pool.get_ptr(0));
    uint64_t expected_offset = reinterpret_cast<char*>(frame->data) - umem_base;
    
    EXPECT_EQ(tx_ring_data[0].addr, expected_offset) << "XDP UMEM translation mismatch";
    EXPECT_EQ(tx_ring_data[0].len, 128);
    
    pool.release(frame);
}