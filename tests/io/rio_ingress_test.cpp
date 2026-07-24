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
#include <iostream>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/io/rio_ingress.hpp"

#pragma comment(lib, "ws2_32.lib")

namespace {

    struct alignas(64) padded_frame_slot {
        transport::raw_tcp_frame frame;
    };

    template <typename T, std::size_t BlockCount>
    class alignas(64) windows_block_allocator {
    private:
        alignas(64) padded_frame_slot* storage_{nullptr};
        alignas(64) std::atomic<std::size_t> allocation_index_{0};
        alignas(64) std::atomic<std::size_t> release_index_{0};

    public:
        explicit windows_block_allocator() noexcept {
            storage_ = reinterpret_cast<padded_frame_slot*>(::VirtualAlloc(
                nullptr, sizeof(padded_frame_slot) * BlockCount, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE
            ));
            for (std::size_t i = 0; i < BlockCount; ++i) {
                new (&storage_[i]) padded_frame_slot();
            }
        }

        ~windows_block_allocator() noexcept {
            if (storage_) {
                for (std::size_t i = 0; i < BlockCount; ++i) {
                    storage_[i].~padded_frame_slot();
                }
                ::VirtualFree(storage_, 0, MEM_RELEASE);
            }
        }

        SLAB_FORCE_INLINE transport::raw_tcp_frame* make_raw() noexcept {
            std::size_t current = allocation_index_.load(std::memory_order_relaxed);
            if (current - release_index_.load(std::memory_order_acquire) >= BlockCount) {
                return nullptr;
            }
            transport::raw_tcp_frame* ptr = &storage_[current % BlockCount].frame;
            allocation_index_.store(current + 1, std::memory_order_relaxed);
            return ptr;
        }

        SLAB_FORCE_INLINE void free(void* address) noexcept {
            if (__builtin_expect(address != nullptr, 1)) {
                release_index_.fetch_add(1, std::memory_order_release);
            }
        }

        SLAB_FORCE_INLINE void* data() noexcept { return static_cast<void*>(storage_); }
        SLAB_FORCE_INLINE std::size_t size_bytes() const noexcept { return sizeof(padded_frame_slot) * BlockCount; }
    };

    inline bool load_rio_extension_table(SOCKET sock, RIO_EXTENSION_FUNCTION_TABLE& table) noexcept {
        GUID rio_extension_guid = WSAID_MULTIPLE_RIO;
        DWORD bytes_returned = 0;
        int result = WSAIoctl(
            sock, SIO_GET_MULTIPLE_EXTENSION_FUNCTION_POINTER,
            &rio_extension_guid, sizeof(rio_extension_guid),
                              &table, sizeof(table),
                              &bytes_returned, nullptr, nullptr
        );
        return result != SOCKET_ERROR;
    }

    inline void create_connected_loopback_pair(SOCKET& client_sock, SOCKET& server_sock) noexcept {
        SOCKET listen_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        client_sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;

        ::bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(listen_sock, 1);

        int addr_len = sizeof(addr);
        ::getsockname(listen_sock, reinterpret_cast<sockaddr*>(&addr), &addr_len);

        u_long mode = 1;
        ::ioctlsocket(client_sock, FIONBIO, &mode);
        ::connect(client_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(client_sock, &write_set);
        timeval tv{5, 0};
        ::select(0, nullptr, &write_set, nullptr, &tv);

        server_sock = ::accept(listen_sock, nullptr, nullptr);
        ::closesocket(listen_sock);
        ::ioctlsocket(server_sock, FIONBIO, &mode);
    }

} // anonymous namespace

// ============================================================================
// METRIC 1: MICROARCHITECTURAL STRIDE & VIRTUAL PAGE-BOUNDARY CEILING AUDIT
// ============================================================================
TEST(RioIngressStressTest, MicroarchitecturalResidencyAudit) {
    using InboundConduit = core::conduit;

    InboundConduit    out_conduit;
    std::atomic<bool> running{true};

    WSADATA wsa_data;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsa_data), 0);

    SOCKET test_socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ASSERT_NE(test_socket, INVALID_SOCKET);

    RIO_EXTENSION_FUNCTION_TABLE rio_table;
    if (!load_rio_extension_table(test_socket, rio_table)) {
        ::closesocket(test_socket);
        WSACleanup();
        GTEST_SKIP() << "Windows RIO Subsystem is not supported on this interface.";
    }

    try {
        transport::rio_ingress<InboundConduit> ingress_engine(
            test_socket, rio_table, out_conduit, running
        );

        EXPECT_EQ(sizeof(padded_frame_slot) % 64, 0);
        EXPECT_EQ(alignof(padded_frame_slot), 64);

        padded_frame_slot test_array[2];
        uintptr_t diff = reinterpret_cast<uintptr_t>(&test_array[1]) - reinterpret_cast<uintptr_t>(&test_array[0]);
        EXPECT_EQ(diff % 64, 0);

    } catch (...) {
        FAIL() << "Failed to construct Registered IO Ingress memory structures.";
    }

    ::closesocket(test_socket);
    WSACleanup();
}

// ============================================================================
// METRIC 2: REAL THROUGHPUT PHYSICS (VALID FLUSHING BOUNDARIES)
// ============================================================================
TEST(RioIngressStressTest, SaturationThroughputPhysics) {
    using InboundConduit = core::conduit;

    InboundConduit    in_conduit;
    std::atomic<bool> running{true};

    WSADATA wsa_data;
    ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsa_data), 0);

    SOCKET tx_client_socket = INVALID_SOCKET;
    SOCKET rx_server_socket = INVALID_SOCKET;
    create_connected_loopback_pair(tx_client_socket, rx_server_socket);

    RIO_EXTENSION_FUNCTION_TABLE rio_table;
    ASSERT_TRUE(load_rio_extension_table(rx_server_socket, rio_table));

    transport::rio_ingress<InboundConduit> ingress_engine(
        rx_server_socket, rio_table, in_conduit, running
    );

    constexpr std::size_t BENCHMARK_ITERATIONS = 40'000;
    alignas(64) char packet_payload[256];
    std::memset(packet_payload, 0x42, sizeof(packet_payload));

    for (int i = 0; i < 64; ++i) {
        ::send(tx_client_socket, packet_payload, sizeof(packet_payload), 0);
    }

    core::tagged_pointer drain_batch[32];
    uint64_t start_cycles = __rdtsc();
    auto start_time = std::chrono::high_resolution_clock::now();

    for (std::size_t i = 0; i < BENCHMARK_ITERATIONS; ++i) {
        ingress_engine.poll_ingress(rio_table);

        if (__builtin_expect(in_conduit.occupancy() >= 16, 0)) {
            in_conduit.pop_batch(drain_batch, 32);
        }

        if (__builtin_expect(i % 2 == 0, 0)) {
            ::send(tx_client_socket, packet_payload, sizeof(packet_payload), 0);
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    uint64_t end_cycles = __rdtsc();

    std::chrono::duration<double> total_duration = end_time - start_time;
    double millions_of_ops_per_sec = (static_cast<double>(BENCHMARK_ITERATIONS) / total_duration.count()) / 1'000'000.0;
    double precision_cycles_per_poll = static_cast<double>(end_cycles - start_cycles) / BENCHMARK_ITERATIONS;

    std::cout << "[PERF] Windows RIO Ingress Validated Throughput: " << millions_of_ops_per_sec << " Mops/sec\n";
    std::cout << "[PERF] Windows RIO Ingress Real Loop Latency: " << precision_cycles_per_poll << " cycles/poll\n";

    EXPECT_
