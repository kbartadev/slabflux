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
 * ============================================================================*
 * @file rio_duplex_test.cpp
 * @brief Windows Registered I/O (RIO) Duplex Stress Verification Suite.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <gtest/gtest.h>
#include <intrin.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <iostream>
#include <random>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/io/rio_ingress_nexus.hpp"
#include "slabflux/io/rio_egress_nexus.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace {

    // Cache-aligned frame wrapper to match high-performance arena layout bounds
    struct alignas(64) pinned_test_frame {
        transport::raw_tcp_frame frame;
        char padding[2048 - sizeof(transport::raw_tcp_frame)];
    };

    /**
     * @brief High-Performance Lock-Free Fixed Arena for RIO buffer simulation.
     */
    template <std::size_t BlockCount>
    class alignas(64) fixed_arena_allocator {
    private:
        pinned_test_frame* memory_block_{nullptr};
        alignas(64) std::atomic<std::size_t> alloc_idx_{0};
        alignas(64) std::atomic<std::size_t> free_idx_{0};

    public:
        explicit fixed_arena_allocator() noexcept {
            memory_block_ = reinterpret_cast<pinned_test_frame*>(::VirtualAlloc(
                nullptr, sizeof(pinned_test_frame) * BlockCount, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE
            ));
            for (std::size_t i = 0; i < BlockCount; ++i) {
                new (&memory_block_[i]) pinned_test_frame();
            }
        }

        ~fixed_arena_allocator() noexcept {
            if (memory_block_) {
                for (std::size_t i = 0; i < BlockCount; ++i) {
                    memory_block_[i].~pinned_test_frame();
                }
                ::VirtualFree(memory_block_, 0, MEM_RELEASE);
            }
        }

        inline transport::raw_tcp_frame* make_raw() noexcept {
            std::size_t curr = alloc_idx_.load(std::memory_order_relaxed);
            if (curr - free_idx_.load(std::memory_order_acquire) >= BlockCount) {
                return nullptr; // Arena exhaustion
            }
            auto* ptr = &memory_block_[curr % BlockCount].frame;
            alloc_idx_.store(curr + 1, std::memory_order_relaxed);
            return ptr;
        }

        inline void release(void* address) noexcept {
            if (__builtin_expect(address != nullptr, 1)) {
                free_idx_.fetch_add(1, std::memory_order_release);
            }
        }

        inline void* data() noexcept { return static_cast<void*>(memory_block_); }
        inline std::size_t size_bytes() const noexcept { return sizeof(pinned_test_frame) * BlockCount; }
    };

    inline bool runtime_load_rio_table(SOCKET s, RIO_EXTENSION_FUNCTION_TABLE& table) noexcept {
        GUID guid = WSAID_MULTIPLE_RIO;
        DWORD bytes = 0;
        return WSAIoctl(s, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
                        &guid, sizeof(guid), &table, sizeof(table),
                        &bytes, nullptr, nullptr) != SOCKET_ERROR;
    }

    inline void establish_low_latency_loopback(SOCKET& tx_sock, SOCKET& rx_sock) noexcept {
        SOCKET listen_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        tx_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        sockaddr_in loopback_addr{};
        loopback_addr.sin_family = AF_INET;
        loopback_addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        loopback_addr.sin_port = 0;

        ::bind(listen_sock, reinterpret_cast<sockaddr*>(&loopback_addr), sizeof(loopback_addr));
        ::listen(listen_sock, 1);

        int len = sizeof(loopback_addr);
        ::getsockname(listen_sock, reinterpret_cast<sockaddr*>(&loopback_addr), &len);

        u_long non_block = 1;
        ::ioctlsocket(tx_sock, FIONBIO, &non_block);
        ::connect(tx_sock, reinterpret_cast<sockaddr*>(&loopback_addr), sizeof(loopback_addr));

        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(tx_sock, &write_fds);
        timeval timeout{2, 0};
        ::select(0, nullptr, &write_fds, nullptr, &timeout);

        rx_sock = ::accept(listen_sock, nullptr, nullptr);
        ::closesocket(listen_sock);
        ::ioctlsocket(rx_sock, FIONBIO, &non_block);

        // Optimize TCP stack layout for microsecond latency tests
        int no_delay = 1;
        ::setsockopt(tx_sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));
        ::setsockopt(rx_sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&no_delay), sizeof(no_delay));
    }

} // anonymous namespace

// ============================================================================
// MICROARCHITECTURAL ALIGNMENT AUDIT
// ============================================================================
TEST(RioDuplexStressTest, MicroarchitecturalResidencyAudit) {
    using DummyConduit = core::spsc_conduit<core::tagged_pointer, 128>;
    using TestArena    = fixed_arena_allocator<128>;

    WSADATA wsa_data;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsa_data), 0);

    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    RIO_EXTENSION_FUNCTION_TABLE rio;
    if (!runtime_load_rio_table(s, rio)) {
        ::closesocket(s);
        WSACleanup();
        GTEST_SKIP() << "Windows Registered I/O extension library is unavailable on this host.";
    }

    // Verify layout boundaries prevent L3 false-sharing cache invalidations
    EXPECT_EQ(sizeof(typename transport::rio_ingress_nexus<DummyConduit, 128>::ingress_slot) % 64, 0);
    EXPECT_EQ(alignof(typename transport::rio_ingress_nexus<DummyConduit, 128>::ingress_slot), 64);
    EXPECT_EQ(alignof(transport::rio_egress_nexus<DummyConduit, TestArena, 128>), 64);

    ::closesocket(s);
    WSACleanup();
}

// ============================================================================
// HIGH-VELOCITY LINE-RATE SATURATION EXPERIMENT
// ============================================================================
TEST(RioDuplexStressTest, BidirectionalVelocitySaturation) {
    using PipelineConduit = core::spsc_conduit<core::tagged_pointer, 1024>;
    using EgressArena     = fixed_arena_allocator<1024>;

    WSADATA wsa_data;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsa_data), 0);

    SOCKET tx_socket = INVALID_SOCKET;
    SOCKET rx_socket = INVALID_SOCKET;
    establish_low_latency_loopback(tx_socket, rx_socket);

    RIO_EXTENSION_FUNCTION_TABLE rio_tx_table;
    RIO_EXTENSION_FUNCTION_TABLE rio_rx_table;
    ASSERT_TRUE(runtime_load_rio_table(tx_socket, rio_tx_table));
    ASSERT_TRUE(runtime_load_rio_table(rx_socket, rio_rx_table));

    PipelineConduit ingress_conduit;
    PipelineConduit egress_conduit;
    EgressArena     tx_arena;
    std::atomic<bool> engine_running{true};

    // Instantiate your optimized low-latency I/O boundaries
    transport::rio_ingress_nexus<PipelineConduit, 512> ingress_nexus(
        rx_socket, rio_rx_table, ingress_conduit, engine_running
    );

    transport::rio_egress_nexus<PipelineConduit, EgressArena, 512> egress_nexus(
        tx_socket, rio_tx_table, egress_conduit, tx_arena, engine_running
    );

    constexpr std::size_t TOTAL_BURST_PACKETS = 50'000;
    constexpr std::size_t PACKET_SIZE_BYTES   = 128;

    // Seed data template
    alignas(64) uint8_t message_blueprint[PACKET_SIZE_BYTES];
    std::random_device rd;
    std::mtu19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 255);
    for (std::size_t i = 0; i < PACKET_SIZE_BYTES; ++i) {
        message_blueprint[i] = static_cast<uint8_t>(dist(gen));
    }

    std::cout << "[START] Driving " << TOTAL_BURST_PACKETS << " zero-copy cycles through RIO Duplex...\n";

    auto start_time = std::chrono::high_resolution_clock::now();
    uint64_t start_cycles = __rdtsc();

    std::size_t packets_sent = 0;
    std::size_t packets_received = 0;

    alignas(64) core::tagged_pointer ingress_drain_batch[32];

    // Main execution path loop simulating real-world execution
    while (packets_received < TOTAL_BURST_PACKETS) {

        // 1. Fill egress conduits up to limits
        if (packets_sent < TOTAL_BURST_PACKETS && egress_conduit.occupancy() < 256) {
            transport::raw_tcp_frame* frame = tx_arena.make_raw();
            if (frame) {
                frame->payload_length = static_cast<uint16_t>(PACKET_SIZE_BYTES);
                frame->connection_id  = static_cast<uint32_t>(tx_socket);
                std::memcpy(frame->data, message_blueprint, PACKET_SIZE_BYTES);

                core::tagged_pointer tok = core::tagged_pointer::pack(transport::raw_tcp_frame::ID, frame);
                if (egress_conduit.try_push(tok)) {
                    packets_sent++;
                } else {
                    tx_arena.release(frame);
                }
            }
        }

        // 2. Poll I/O layers
        egress_nexus.poll_egress(rio_tx_table);
        ingress_nexus.poll_ingress(rio_rx_table);

        // 3. Process and drain the ingress pipeline conduit
        std::size_t rx_count = ingress_conduit.pop_batch(ingress_drain_batch, 32);
        if (rx_count > 0) {
            for (std::size_t i = 0; i < rx_count; ++i) {
                auto* rx_frame = reinterpret_cast<transport::raw_tcp_frame*>(ingress_drain_batch[i].get_address());
                if (rx_frame) {
                    // Match contents against seed block to check for memory drift or corrupt copies
                    EXPECT_EQ(rx_frame->payload_length, PACKET_SIZE_BYTES);
                    EXPECT_EQ(rx_frame->data[0], message_blueprint[0]);
                    EXPECT_EQ(rx_frame->data[PACKET_SIZE_BYTES - 1], message_blueprint[PACKET_SIZE_BYTES - 1]);

                    // Execute ownership release contract back to RIO memory maps
                    auto* slot_ptr = reinterpret_cast<typename transport::rio_ingress_nexus<PipelineConduit, 512>::ingress_slot*>(
                        reinterpret_cast<char*>(rx_frame) - offsetof(typename transport::rio_ingress_nexus<PipelineConduit, 512>::ingress_slot, frame)
                    );
                    slot_ptr->leased.store(2, std::memory_order_release);
                    packets_received++;
                }
            }
        }
    }

    uint64_t end_cycles = __rdtsc();
    auto end_time = std::chrono::high_resolution_clock::now();

    // Calculate core processing speed
    std::chrono::duration<double> duration = end_time - start_time;
    double mops = (static_cast<double>(TOTAL_BURST_PACKETS) / duration.count()) / 1'000'000.0;
    double cycles_per_packet = static_cast<double>(end_cycles - start_cycles) / TOTAL_BURST_PACKETS;

    std::cout << "[PERF] RIO Duplex Sustained Velocity: " << mops << " Million messages/sec\n";
    std::cout << "[PERF] Microarchitectural Cost: " << cycles_per_packet << " CPU cycles/packet\n";

    // Enforce high-performance targets (Must execute within sub-nanosecond/low-cycle parameters)
    EXPECT_GT(mops, 1.5);
    EXPECT_LT(cycles_per_packet, 180.0);

    engine_running.store(false);
    ::closesocket(tx_socket);
    ::closesocket(rx_socket);
    WSACleanup();
}
