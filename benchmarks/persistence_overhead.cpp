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
 * @file persistence_overhead.cpp
 */

#include <benchmark/benchmark.h>
#include <thread> // Required for std::thread::hardware_concurrency() and std::this_thread::yield()
#include <filesystem>
#include <cstring>
#include "slabflux/core/hardware_topology.hpp" // For pin_thread
#include "slabflux/io/durable_journal.hpp"
#include "slabflux/io/io_uring_durable_journal.hpp" // New io_uring journal
#include "slabflux/storage/durable_sink.hpp" // Updated durable_sink

using namespace slabflux::storage; // For durable_sink

using namespace slabflux::io;

/**
 * @brief Measures the raw throughput of the Durable Sink using Zero-Copy.
 */
static void BM_Durable_Sink_Throughput(benchmark::State& state) {
    // Each thread gets its own log file so there is no lock contention on the filesystem
    const std::string log_path = "bench_events_t" + std::to_string(state.thread_index()) + ".alog";

    struct alignas(4096) raw_frame {
        uint64_t timestamp;
        char data[128];
    };

    // 2 GB arena
    constexpr size_t ArenaSize = 2147483648ULL;
    
    durable_sink<raw_frame, slabflux::io::durable_journal<raw_frame>> sink(log_path.c_str()); // Using mmap-based journal
    raw_frame sample{ .timestamp = 0xDEADBEEF };

    // The loop runs as long as GBM requests it. Since we limit the iteration count
    // at registration time, reserve_slot is guaranteed to never return nullptr.
    for (auto _ : state) {
        raw_frame* slot = sink.reserve_slot();
        
        if (__builtin_expect(slot != nullptr, 1)) {
            std::memcpy(slot, &sample, sizeof(raw_frame));
            sink.commit(); // Changed to commit()
            benchmark::DoNotOptimize(slot);
            benchmark::ClobberMemory();
        } else {
            state.SkipWithError("Arena exhaustion!");
            break;
        }
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * sizeof(raw_frame));
    
    if (std::filesystem::exists(log_path)) {
        std::filesystem::remove(log_path);
    }
}

BENCHMARK(BM_Durable_Sink_Throughput)->ThreadRange(1, 4)->Iterations(100000)->UseRealTime();

/**
 * @brief Measures the raw throughput of the io_uring Durable Sink using Zero-Copy.
 * This version leverages io_uring with SQ_AFF for absolute lowest jitter.
 */
static void BM_IoUring_Durable_Sink_Throughput(benchmark::State& state) {
    // Each thread gets its own log file so there is no lock contention on the filesystem
    const std::string log_path = "bench_io_uring_events_t" + std::to_string(state.thread_index()) + ".alog";

    struct alignas(4096) raw_frame {
        uint64_t timestamp;
        char data[128];
    };

    // 2 GB arena
    constexpr size_t ArenaSize = 2147483648ULL;
    
    // FIX: Optimized core distribution. Benchmark threads occupy lower cores,
    // so we place the kernel I/O pollers on the upper half of the topology.
    const int sq_cpu = (std::thread::hardware_concurrency() / 2 + state.thread_index()) % (int)std::thread::hardware_concurrency();
    
    durable_sink<raw_frame, io_uring_durable_journal<raw_frame>> sink(log_path.c_str(), sq_cpu);
    raw_frame sample{ .timestamp = 0xDEADBEEF };

    for (auto _ : state) {
        raw_frame* slot = sink.reserve_slot();
        
        if (__builtin_expect(slot != nullptr, 1)) {
            std::memcpy(slot, &sample, sizeof(raw_frame));
            sink.commit();
            benchmark::DoNotOptimize(slot);
            benchmark::ClobberMemory();
        } else {
            state.SkipWithError("Arena exhaustion!");
            break;
        }
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * sizeof(raw_frame));
    
    if (std::filesystem::exists(log_path)) {
        std::filesystem::remove(log_path);
    }
}

BENCHMARK(BM_IoUring_Durable_Sink_Throughput)
    ->ThreadRange(1, 4)
    ->Iterations(100000) // Exactly 100k iterations per thread. No overflow, no reset!
    ->UseRealTime();

/**
 * @brief Measures the latency impact of Zero-Copy persistence.
 */
static void BM_Persistence_Latency_Probe(benchmark::State& state) {
    const std::string log_path = "latency_events.alog";
    durable_sink<uint64_t, durable_journal<uint64_t>> sink(log_path.c_str()); // Using mmap-based journal

    for (auto _ : state) {
        uint64_t* slot = sink.reserve_slot();
        
        // Reset removed here to measure raw hardware path latency
        if (__builtin_expect(slot != nullptr, 1)) {
            *slot = 12345; // Assign a value
            sink.commit(); // Changed to commit()
            benchmark::DoNotOptimize(slot);
            benchmark::ClobberMemory();
        } else {
            state.SkipWithError("Arena exhaustion!");
            break;
        }
    }

    std::filesystem::remove(log_path);
}
BENCHMARK(BM_Persistence_Latency_Probe)->UseRealTime();

// Measures the latency impact of io_uring Zero-Copy persistence.
static void BM_IoUring_Persistence_Latency_Probe(benchmark::State& state) {
    const std::string log_path = "io_uring_latency_events.alog";
    // Use the io_uring-backed journal, pinning to core 3 for I/O operations.
    const int sq_cpu = (8 + state.thread_index()) % (int)std::thread::hardware_concurrency();
    
    durable_sink<uint64_t, io_uring_durable_journal<uint64_t>> sink(log_path.c_str(), sq_cpu);

    for (auto _ : state) {
        uint64_t* slot = sink.reserve_slot();
        
        if (__builtin_expect(slot != nullptr, 1)) {
            *slot = 12345;
            sink.commit();
            benchmark::DoNotOptimize(slot);
            benchmark::ClobberMemory();
        } else {
            state.SkipWithError("Arena exhaustion!");
            break;
        }
    }

    std::filesystem::remove(log_path);
}
BENCHMARK(BM_IoUring_Persistence_Latency_Probe)->UseRealTime();

// Measures the overhead of force_flush, which ensures data is written to disk.
static void BM_Durable_Journal_ForceFlush(benchmark::State& state) {
    const std::string log_path = "flush_events.alog";
    durable_sink<uint64_t, durable_journal<uint64_t>> sink(log_path.c_str()); // Using mmap-based journal

    // Fill the buffer with some data before measuring flush
    for (int i = 0; i < 1000; ++i) {
        if (uint64_t* slot = sink.reserve_slot()) {
            *slot = i;
            sink.commit();
        }
    }

    for (auto _ : state) {
        // Now performs actual msync(MS_SYNC) to measure the physical 
        // cost of mmap-based per-event durability.
        sink.force_flush();
    }

    std::filesystem::remove(log_path);
}
BENCHMARK(BM_Durable_Journal_ForceFlush)->UseRealTime();

// Measures the overhead of force_flush for the io_uring journal.
static void BM_IoUring_Durable_Journal_ForceFlush(benchmark::State& state) {
    const std::string log_path = "io_uring_flush_events.alog";
    // Use the io_uring-backed journal, pinning to core 3 for I/O operations.
    const int sq_cpu = (8 + state.thread_index()) % (int)std::thread::hardware_concurrency();
    
    durable_sink<uint64_t, io_uring_durable_journal<uint64_t>> sink(log_path.c_str(), sq_cpu);

    // Fill the buffer with some data before measuring flush
    for (int i = 0; i < 1000; ++i) {
        if (uint64_t* slot = sink.reserve_slot()) {
            *slot = i;
            sink.commit();
        }
    }

    for (auto _ : state) {
        sink.force_flush(); // This will now call the io_uring_durable_journal's force_flush()
    }

    std::filesystem::remove(log_path);
}
BENCHMARK(BM_IoUring_Durable_Journal_ForceFlush)->UseRealTime();

BENCHMARK_MAIN();