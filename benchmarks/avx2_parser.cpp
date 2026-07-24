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
#include <immintrin.h>
#include <string_view>
#include <string>
#include "slabflux/hw/intrinsics.hpp"
#include "slabflux/core/hot_path_alignment.hpp"

#ifndef _WIN32
#include "slabflux/io/uring_shim.hpp" // For uring_shim functions
#include <liburing.h>
#endif

// ============================================================================
// BENCHMARK ENVIRONMENT MOCKS (Satisfies isolated compilation requirements)
// ============================================================================
#ifndef _WIN32
inline io_uring ring_{};
#endif
inline int wal_fd_ = -1;

struct {
    std::atomic<uint64_t> active_mask{ 0 };
} cluster_state_;

void execute_deterministic_core(std::string_view) {}

enum { INTENT_ORCHESTRATION = 1, INTENT_DURABLE_BROKER = 2, INTENT_DETERMINISTIC = 3 };

alignas(32) const char benchmark_payload[128] =
"Host: 127.0.0.1:8080\r\n"
"Accept: text/html\r\n"
"Connection: keep-alive\r\n\r\n";

static void BM_StandardStringFind(benchmark::State& state) {
    std::string_view payload(benchmark_payload);
    for (auto _ : state) {
        std::size_t start = 0;
        while (start < payload.size()) {
            std::size_t nl_pos = payload.find('\n', start);
            if (nl_pos == std::string_view::npos) break;

            std::size_t colon_pos = payload.find(':', start);
            if (colon_pos != std::string_view::npos && colon_pos < nl_pos) {
                std::string_view name = payload.substr(start, colon_pos - start);
                std::string_view value = payload.substr(colon_pos + 2, nl_pos - colon_pos - 3);
                benchmark::DoNotOptimize(name);
                benchmark::DoNotOptimize(value);
            }
            start = nl_pos + 1;
        }
    }
}
BENCHMARK(BM_StandardStringFind);

static void BM_Avx2BitmaskIteration(benchmark::State& state) {
    const __m256i v_newline = _mm256_set1_epi8('\n');
    const __m256i v_colon = _mm256_set1_epi8(':');

    for (auto _ : state) {
        __m256i chunk = _mm256_load_si256(reinterpret_cast<const __m256i*>(benchmark_payload));
        uint32_t nl_mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_newline));
        uint32_t colon_mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, v_colon));

        uint32_t current_start_idx = 0;

        while (nl_mask) {
            // Using hardware abstraction
            uint32_t nl_idx = slabflux::hw::tzcnt_32(nl_mask);
            uint32_t line_mask = (1ULL << nl_idx) - (1ULL << current_start_idx);
            uint32_t valid_colons = colon_mask & line_mask;

            if (valid_colons) {
                // Using hardware abstraction
                uint32_t colon_idx = slabflux::hw::tzcnt_32(valid_colons);
                std::string_view name(benchmark_payload + current_start_idx, colon_idx - current_start_idx);
                std::string_view value(benchmark_payload + colon_idx + 2, nl_idx - colon_idx - 3);

                benchmark::DoNotOptimize(name);
                benchmark::DoNotOptimize(value);
            }

            current_start_idx = nl_idx + 1;
            nl_mask &= (nl_mask - 1);
        }
    }
}
BENCHMARK(BM_Avx2BitmaskIteration);

// The SIMD logic from bench_avx2_parser.cpp fused into the Synapse
SLAB_FORCE_INLINE uint64_t parse_intent_avx2(std::string_view payload) noexcept {
    // If the packet is too short, immediately hand off to Orchestration
    if (payload.size() < 32) [[unlikely]] return INTENT_ORCHESTRATION;

    // 1. LOAD (1 cycle): Load the first 32 bytes from L1 cache into the YMM register
    __m256i chunk = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(payload.data()));

    // 2. The “MAGIC” HEADERS (Little-endian 64-bit form)
    // e.g.: "BROKER  " = 0x202052454B4F5242
    //       "COMPUTE " = 0x20455455504D4F43
    __m256i target_broker = _mm256_set1_epi64x(0x202052454B4F5242ULL);
    __m256i target_compute = _mm256_set1_epi64x(0x20455455504D4F43ULL);

    // 3. PARALLEL COMPARISON (SIMD Compare)
    __m256i cmp_broker = _mm256_cmpeq_epi64(chunk, target_broker);
    __m256i cmp_compute = _mm256_cmpeq_epi64(chunk, target_compute);

    // 4. EXTRACT BITMASK
    uint32_t mask_broker = _mm256_movemask_epi8(cmp_broker);
    uint32_t mask_compute = _mm256_movemask_epi8(cmp_compute);

    // 5. BRANCHLESS DECISION
    uint64_t final_intent = 0;
    final_intent |= (mask_broker != 0) ? INTENT_DURABLE_BROKER : 0;
    final_intent |= (mask_compute != 0) ? INTENT_DETERMINISTIC : 0;

    // Fallback: If network traffic but no match, route to Platform
    final_intent |= (final_intent == 0) ? INTENT_ORCHESTRATION : 0;

    return final_intent;
}

// Durable Message Broker: Zero-copy, Zero-syscall SSD write
SLAB_FORCE_INLINE void submit_nvme_write_async(std::string_view payload) noexcept {

// 1. Request a free Submission Queue Entry from the running ring
#ifndef _WIN32
    // Original Linux io_uring code
    io_uring_sqe* sqe = slabflux::io::uring_shim::get_sqe(&ring_);

    if (sqe) [[likely]] {
        // 2. Configure O_DIRECT write
        io_uring_prep_write(sqe, wal_fd_, payload.data(), payload.size(), -1);

        // 3. ASYNCHRONOUS OFFLOAD (KEY TO ZERO-STALL)
        sqe->flags |= IOSQE_ASYNC;

        // Performance:
        // There is intentionally NO io_uring_submit(ring_) here.
        // Submission is batched by matrix_nexus for all packets.
    }
#else
    // Windows fallback or NOP for benchmark consistency
    (void)0;
#endif
}

SLAB_FORCE_INLINE void on_fast_path(std::string_view header, std::string_view payload) noexcept {

    // 1 cycle: What is this packet? (AVX2)
    uint64_t intent_mask = parse_intent_avx2(payload);

    // Parallel, branchless execution:

    if (intent_mask & INTENT_DURABLE_BROKER) {
        submit_nvme_write_async(payload); // -> Goes straight to NVMe SSD
    }

    if (intent_mask & INTENT_ORCHESTRATION) {
        cluster_state_.active_mask.fetch_or(1ULL, std::memory_order_relaxed);
    }

    if (intent_mask & INTENT_DETERMINISTIC) {
        execute_deterministic_core(payload);
    }
}

BENCHMARK_MAIN();
