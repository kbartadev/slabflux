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
#include <liburing.h>
#include <atomic>
#include <vector>
#include <stdexcept>
#include <thread>
#include "slabflux/io/uring_shim.hpp" // For uring_shim functions
#include <chrono>

#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

using namespace slabflux;
using namespace slabflux::io;

// Mock event structure
struct mock_event {
    uint64_t data[8]; // 64 bytes
};

// Mock io_uring setup for measuring internal overheads
class MockUring {
public:
    io_uring ring;
    static constexpr int QUEUE_DEPTH = 256;

    MockUring(int cpu_id) {
        io_uring_params params{};
        params.flags |= IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF;
        params.sq_thread_cpu = cpu_id;
        slabflux::io::uring_shim::ring_init(QUEUE_DEPTH, &ring, params.flags, 0); // sq_idle = 0 for default
    }

    ~MockUring() {
        io_uring_queue_exit(&ring);
    }
};

// Benchmark for io_uring SQE submission overhead (no actual I/O)
static void BM_IoUring_SqeSubmission(benchmark::State& state) {
    const int sq_cpu = state.thread_index() % 4;
    MockUring mock_uring(sq_cpu);
    
    // Pin the user thread to a different core than the kernel poller (e.g., offset by 4)
    slabflux::core::hardware_topology::pin_thread((sq_cpu + 4) % 12);

    mock_event ev_data;

    // We'll submit in batches to better reflect real-world io_uring usage
    constexpr int SUBMISSION_BATCH_SIZE = MockUring::QUEUE_DEPTH / 4; // Submit smaller batches
    int sqe_count = 0;

    for (auto _ : state) {
        io_uring_sqe* sqe = slabflux::io::uring_shim::get_sqe(&mock_uring.ring);
        while (SL_UNLIKELY(!sqe)) { // Spin-wait until SQE is available
            // Submit any pending SQEs to give the kernel poller work
            if (sqe_count > 0) {
                slabflux::io::uring_shim::submit(&mock_uring.ring);
                sqe_count = 0;
            }
            
            // Reap completions to free up SQE slots
            io_uring_cqe* cqe;
            while (io_uring_peek_cqe(&mock_uring.ring, &cqe) == 0) {
                ::io_uring_cqe_seen(&mock_uring.ring, cqe); // Not in shim, fine.
            }
            
            _mm_pause(); // Yield to allow kernel poller to run
            sqe = slabflux::io::uring_shim::get_sqe(&mock_uring.ring);
        }
        // Prepare a dummy NOP operation
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(&ev_data));
        sqe_count++;

        // Submit in batches or if the queue is almost full
        if (sqe_count >= SUBMISSION_BATCH_SIZE) {
            slabflux::io::uring_shim::submit(&mock_uring.ring);
            sqe_count = 0;
        }
    }

    // Ensure all remaining SQEs are submitted at the end of the benchmark
    if (sqe_count > 0) {
        slabflux::io::uring_shim::submit(&mock_uring.ring);
    }

    // Reap all completions to clean up
    io_uring_cqe* cqe;
    while (io_uring_peek_cqe(&mock_uring.ring, &cqe) == 0) {
        ::io_uring_cqe_seen(&mock_uring.ring, cqe); // Not in shim, fine.
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_IoUring_SqeSubmission)->ThreadRange(1, 4)->UseRealTime();

// Benchmark for io_uring CQE reaping overhead (no actual I/O)
static void BM_IoUring_CqeReaping(benchmark::State& state) {
    const int sq_cpu = state.thread_index() % 4;
    MockUring mock_uring(sq_cpu);
    
    slabflux::core::hardware_topology::pin_thread((sq_cpu + 4) % 12);

    mock_event ev_data;

    // Pre-fill the CQ with some dummy completions
    for (int i = 0; i < MockUring::QUEUE_DEPTH / 2; ++i) {
        io_uring_sqe* sqe = io_uring_get_sqe(&mock_uring.ring);
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data64(sqe, reinterpret_cast<uint64_t>(&ev_data));
    }
    io_uring_submit(&mock_uring.ring);

    for (auto _ : state) {
        io_uring_cqe* cqe;
        while (io_uring_peek_cqe(&mock_uring.ring, &cqe) == 0) {
            io_uring_cqe_seen(&mock_uring.ring, cqe); // This is the operation being measured
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_IoUring_CqeReaping)->ThreadRange(1, 4)->UseRealTime();

BENCHMARK_MAIN();