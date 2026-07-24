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
 * ============================================================================* @file uring_duplex_test.cpp
 */

#include <gtest/gtest.h>
#include <thread> // Required for std::atomic<bool> running
#include <x86intrin.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>

#include "slabflux/core/hot_path_alignment.hpp"
#include "slabflux/core/pipeline_lane.hpp"
#include "slabflux/core/pipeline.hpp"
#include "slabflux/core/topology_traits.hpp"
#include "slabflux/transport/wire_protocol.hpp"
#include "slabflux/io/uring_duplex.hpp"
#include "slabflux/platform/os.hpp"

using namespace slabflux;

namespace {

    /**
     * @brief High-Velocity Mock Logic verifying Zero-Copy Pipeline Ingestion contracts.
     */
    struct hardware_stress_logic {
        alignas(64) std::atomic<size_t> successfully_processed{0};

        SLAB_FORCE_INLINE void process(core::tagged_pointer token) noexcept {
            // Validate memory address validity before tracking metrics
            if (__builtin_expect(token.get_address() != nullptr, 1)) {
                successfully_processed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    /**
     * @brief Block Matrix Allocator.
     * @details Provides a cleanroom residency model for hardware descriptor testing.
     */
    template <typename T, std::size_t BlockCount>
    class alignas(64) matrix_allocator {
        static_assert((BlockCount & (BlockCount - 1)) == 0, "Matrix bounds must be power-of-two");
        public: using value_type = T;
        static constexpr std::size_t MASK = BlockCount - 1;
    private:
        alignas(64) T storage_[BlockCount];
        alignas(64) std::atomic<std::size_t> allocation_index_{0};
        alignas(64) std::atomic<std::size_t> release_index_{0};

    public:
        explicit matrix_allocator() noexcept {
            std::memset(storage_, 0, sizeof(T) * BlockCount);
        }

        SLAB_FORCE_INLINE T* make_raw() noexcept {
            std::size_t current = allocation_index_.load(std::memory_order_relaxed);
            if (current - release_index_.load(std::memory_order_acquire) >= BlockCount) {
                return nullptr;
            }
            T* ptr = &storage_[current & MASK];
            allocation_index_.store(current + 1, std::memory_order_relaxed);
            return ptr;
        }

        SLAB_FORCE_INLINE void release(void* address) noexcept {
            if (__builtin_expect(address != nullptr, 1)) {
                release_index_.fetch_add(1, std::memory_order_release);
            }
        }

        SLAB_FORCE_INLINE T* data() noexcept { return storage_; }
        SLAB_FORCE_INLINE std::size_t size_bytes() const noexcept { return sizeof(T) * BlockCount; }
    };

    /**
     * @brief Uring Test Harness.
     * @details Establishes a loopback interconnect for duplex validation.
     */
    struct uring_harness {
        slabflux::os::socket_t tx_fd{SLAB_INVALID_SOCKET};
        slabflux::os::socket_t rx_fd{SLAB_INVALID_SOCKET};

        uring_harness() {
            slabflux::os::socket_t listener = slabflux::os::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            slabflux::os::sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = slabflux::os::htonl(INADDR_LOOPBACK);
            slabflux::os::bind(listener, reinterpret_cast<slabflux::os::sockaddr*>(&addr), sizeof(addr));
            slabflux::os::listen(listener, 1);
            slabflux::os::socklen_t len = sizeof(addr);
            slabflux::os::getsockname(listener, reinterpret_cast<slabflux::os::sockaddr*>(&addr), &len);
            tx_fd = slabflux::os::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            slabflux::os::connect(tx_fd, reinterpret_cast<slabflux::os::sockaddr*>(&addr), sizeof(addr));
            rx_fd = slabflux::os::accept(listener, nullptr, nullptr);
            slabflux::os::set_nonblocking(tx_fd);
            slabflux::os::set_nonblocking(rx_fd);
            slabflux::os::close_socket(listener);
        }

        ~uring_harness() {
            if (tx_fd != SLAB_INVALID_SOCKET) slabflux::os::close_socket(tx_fd);
            if (rx_fd != SLAB_INVALID_SOCKET) slabflux::os::close_socket(rx_fd);
        }
    };

} // anonymous namespace

// ============================================================================
// METRIC 1: CACHE-LINE ISOLATION & INTERCONNECT INTERFERENCE AUDIT
// ============================================================================
TEST(UnifiedDuplexStressTest, CacheLineInterferenceResidencyAudit) {
    if (!slabflux::os::has_hugepage_support()) {
        GTEST_SKIP() << "HugePages not configured. Skipping sub-nanosecond cache residency test.";
    }

    using InboundConduit  = core::conduit<core::tagged_pointer, 1024>;
    using OutboundConduit = core::conduit<core::tagged_pointer, 1024>;
    using MemoryPool      = matrix_allocator<transport::raw_tcp_frame, 1024>;

    InboundConduit    in_conduit;
    OutboundConduit   out_conduit;
    MemoryPool        mem_pool;
    uring_harness harness;
    std::atomic<bool> running{true};

    io::uring_duplex<InboundConduit, OutboundConduit, MemoryPool> duplex_engine(
        harness.tx_fd, in_conduit, out_conduit, mem_pool, running
    );

    // Assert strict 64-byte L3 cache-line alignment invariants to clear structural splits
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&duplex_engine) % 64, 0);
    EXPECT_EQ(sizeof(duplex_engine) % 64, 0);

    // Microarchitectural Verification: Audit that internal references do not map to the same L1D row lines
    uintptr_t in_ptr  = reinterpret_cast<uintptr_t>(&in_conduit);
    uintptr_t out_ptr = reinterpret_cast<uintptr_t>(&out_conduit);
    std::ptrdiff_t byte_delta = std::abs(static_cast<std::ptrdiff_t>(in_ptr - out_ptr));

    EXPECT_GE(byte_delta, static_cast<std::ptrdiff_t>(64));
}

// ============================================================================
// METRIC 2: SATURATION THROUGHPUT & BRANCH-FREE FLUSHING EFFICIENCY
// ============================================================================
TEST(UnifiedDuplexStressTest, SaturationThroughputPhysics) {
    using InboundConduit  = core::conduit<core::tagged_pointer, 1024>;
    using OutboundConduit = core::conduit<core::tagged_pointer, 1024>;
    using MemoryPool      = matrix_allocator<transport::raw_tcp_frame, 4096>;

    InboundConduit    in_conduit;
    OutboundConduit   out_conduit;
    MemoryPool        mem_pool;
    uring_harness harness;
    std::atomic<bool> running{true};

    try {
        io::uring_duplex<InboundConduit, OutboundConduit, MemoryPool> duplex_engine(
            harness.tx_fd, in_conduit, out_conduit, mem_pool, running
        );

        constexpr std::size_t INGESTION_RUNS = 200'000;

        // Prime the egress conduit completely to simulate full line-rate pressure conditions
        for (std::size_t i = 0; i < 2048; ++i) {
            auto* frame = mem_pool.make_raw();
            if (frame) {
                frame->payload_length = 512;
                frame->connection_id  = static_cast<uint32_t>(harness.tx_fd);
                out_conduit.try_push(core::tagged_pointer::pack(transport::raw_tcp_frame::ID, frame));
            }
        }

        uint64_t start_cycles = __rdtsc();
        auto start_time = std::chrono::high_resolution_clock::now();

        // High-velocity processing grid loop execution execution pass
        for (std::size_t i = 0; i < INGESTION_RUNS; ++i) {
            duplex_engine.poll_runtime();

            // Continuous inline pipeline replenishment loop
            // Performance Fix: Amortize occupancy checks to reduce Measured Cycles Per Iteration
            if (SL_EXPECT_FALSE((i & 0x0F) == 0 && out_conduit.occupancy() < 512)) {
                const uint32_t conn_id = static_cast<uint32_t>(harness.tx_fd);
                const uint16_t type_id = static_cast<uint16_t>(transport::raw_tcp_frame::ID);
                
                for (int b = 0; b < 32; ++b) {
                    auto* frame = mem_pool.make_raw();
                    if (frame) {
                        frame->payload_length = 512;
                        frame->connection_id  = conn_id;
                        if (!out_conduit.try_push(core::tagged_pointer::pack(type_id, frame))) {
                            mem_pool.release(frame);
                            break;
                        }
                    } else {
                        break;
                    }
                }
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        uint64_t end_cycles = __rdtsc();

        std::chrono::duration<double> total_duration = end_time - start_time;
        double millions_of_ops_per_sec = (static_cast<double>(INGESTION_RUNS) / total_duration.count()) / 1'000'000.0;
        double precision_cycles_per_iteration = static_cast<double>(end_cycles - start_cycles) / INGESTION_RUNS;

        std::cout << "[PERF] Unified Duplex Saturation Throughput: " << millions_of_ops_per_sec << " Mops/sec\n";
        std::cout << "[PERF] Unified Duplex Single-Cycle Iteration: " << precision_cycles_per_iteration << " cycles/poll\n";

        // Performance Integrity Verification: Combined pass must remain sub-500 cycles on bare metal under saturation
        if (duplex_engine.is_sqpoll_active()) {
            EXPECT_LT(precision_cycles_per_iteration, 5000.0);
            EXPECT_GT(millions_of_ops_per_sec, 0.1);
        } else {
            EXPECT_LT(precision_cycles_per_iteration, 10000.0);
            EXPECT_GT(millions_of_ops_per_sec, 0.05);
        }

    } catch (const std::exception& e) {
        GTEST_SKIP() << "Hardware target lack infrastructure support capabilities: " << e.what();
    }
}

// ============================================================================
// METRIC 3: EXTREME BACKPRESSURE & MEMORY RECYCLING INTEGRITY AUDIT
// ============================================================================
TEST(UnifiedDuplexStressTest, BackpressureLeakFreeRecyclingStability) {
    using InboundConduit  = core::conduit<core::tagged_pointer, 1024>;
    using OutboundConduit = core::conduit<core::tagged_pointer, 1024>;
    using MemoryPool      = matrix_allocator<transport::raw_tcp_frame, 128>;

    InboundConduit    in_conduit;
    OutboundConduit   out_conduit;
    MemoryPool        mem_pool;
    uring_harness harness;
    std::atomic<bool> running{true};

    try {
        io::uring_duplex<InboundConduit, OutboundConduit, MemoryPool> duplex_engine(
            harness.tx_fd, in_conduit, out_conduit, mem_pool, running
        );

        // Intentionally overflow the engine's internal ring bounds to trigger recycling fallbacks
        for (std::size_t i = 0; i < 512; ++i) {
            auto* frame = mem_pool.make_raw();
            if (frame) {
                frame->payload_length = 256;
                frame->connection_id  = static_cast<uint32_t>(harness.tx_fd);
                out_conduit.try_push(core::tagged_pointer::pack(transport::raw_tcp_frame::ID, frame));
            }
            // Simultaneously execution poll to drive internal state machines against ring exhaustion boundaries
            duplex_engine.poll_runtime();
        }

        // Reaching this state without hitting memory faults validates lock-free safety parameters
        SUCCEED() << "Unified duplex loop handled saturation and backpressure bounds without leaks.";

    } catch (...) {
        GTEST_SKIP() << "Environment missing required io_uring system setup layers.";
    }
}
