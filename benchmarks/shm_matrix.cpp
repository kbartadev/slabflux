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
#include <vector>
#include <memory>
#include <x86intrin.h>

#include "slabflux/core/hardware_topology.hpp"
#include "slabflux/io/shm_duplex.hpp"
#include "slabflux/io/shm_arena_duplex.hpp"
#include "slabflux/io/shm_inline_duplex.hpp"
#include "slabflux/bridge/shm_bridge.hpp"
#include "slabflux/io/shm_ingress.hpp"
#include "slabflux/io/shm_egress.hpp"
#include "slabflux/io/shm_journal_duplex.hpp"
#include "slabflux/core/spsc_conduit.hpp"
#include "slabflux/core/pinned_allocator_spsc.hpp"
#include "slabflux/transport/wire_protocol.hpp"

using namespace slabflux;
using namespace slabflux::bridge; // Add this line to bring shm_bridge into scope
using namespace slabflux::io;

/**
 * @brief Benchmarks the standard shm_duplex throughput.
 * Measures the cost of meta fusion and non-temporal streaming.
 */
static void BM_shm_duplex_throughput(benchmark::State& state) {
    slabflux::core::hardware_topology::pin_thread(1);

    auto primary = std::make_unique<shm_duplex<1024>>("bench_shm", true);
    auto conduit = std::make_unique<core::spsc_conduit<core::tagged_pointer, 1024>>();
    
    // Hardening: Move large memory allocators to the heap to prevent 
    // stack overflows (1.5MB+ structure).
    auto pool = std::make_unique<core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>>();

    for (auto _ : state) {
        // BURST REPLENISHMENT: Töltsük fel a conduit-ot 32 elemmel
        for (size_t i = 0; i < 32; ++i) {
            auto* f = pool->make_raw();
            if (f) {
                f->payload_length = 64; // Legyen paritásban az egress méréssel
                conduit->push(core::tagged_pointer::pack(1, f));
            }
        }
        primary->process_egress_burst(*conduit, *pool);
    }
    state.SetItemsProcessed(state.iterations() * 32);
}
BENCHMARK(BM_shm_duplex_throughput);

/**
 * @brief Benchmarks the underlying shm_bridge primitive.
 * Measures the absolute overhead of the lock-free SPSC ring across the SHM boundary.
 */
static void BM_shm_bridge_throughput(benchmark::State& state) {
    slabflux::core::hardware_topology::pin_thread(1);
    using EventType = uint64_t;
    auto bridge = std::make_unique<shm_bridge<EventType, 1024>>("bench_bridge", ipc_role::creator);
    auto& wire = bridge->wire();

    for (auto _ : state) {
        auto* slot = wire.reserve();
        if (slot) {
            *slot = 42;
            wire.commit();
        }
        auto* read = wire.peek();
        if (read) {
            uint64_t val = *read;
            benchmark::DoNotOptimize(val);
            wire.consume();
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_shm_bridge_throughput);

/**
 * @brief Benchmarks high-level shm_ingress polling.
 */
static void BM_shm_ingress_throughput(benchmark::State& state) {
    slabflux::core::hardware_topology::pin_thread(1);
    using EventType = transport::raw_tcp_frame;
    struct mock_logic {
        bool on_raw_frame(const EventType&, size_t) noexcept { return true; }
    } logic;

    auto bridge = std::make_unique<shm_bridge<EventType, 1024>>("bench_in_bridge", ipc_role::creator);
    shm_ingress<EventType, 1024, mock_logic> ingress("bench_in_bridge", ipc_role::joiner, logic);

    for (auto _ : state) {
        // PIPELINE REPLENISHMENT: Minden körben meg kell tölteni a burst-öt,
        // különben csak az üres loop sebességét méred (0.4ns).
        for (size_t i = 0; i < 32; ++i) {
            auto* slot = bridge->wire().reserve_at(i);
            while (SL_UNLIKELY(!slot)) {
                _mm_pause();
                slot = bridge->wire().reserve_at(i);
            }
            slot->payload_length = 64;
        }
        // Amortizált commit (ez szimulálja a termelőt)
        bridge->wire().commit_n(32);

        // Valódi feldolgozás
        ingress.poll();
    }
    state.SetItemsProcessed(state.iterations() * 32); // Each poll processes up to 32 items
}
BENCHMARK(BM_shm_ingress_throughput);
/**
 * @brief Benchmarks high-level shm_egress polling with conduit ingestion.
 */
static void BM_shm_egress_throughput(benchmark::State& state) {
    slabflux::core::hardware_topology::pin_thread(1);
    using EventType = transport::raw_tcp_frame;
    auto conduit = std::make_unique<core::spsc_conduit<EventType*, 1024>>();
    auto pool = std::make_unique<core::pinned_allocator_spsc<EventType, 1024>>();
    
    shm_egress<EventType, 1024, core::spsc_conduit<EventType*, 1024>, core::pinned_allocator_spsc<EventType, 1024>> 
        egress("bench_egress_bridge", ipc_role::creator, *conduit, *pool);

    for (auto _ : state) {
        // PIPELINE REPLENISHMENT: Minden körben meg kell tölteni a burst-öt,
        // különben csak az üres loop sebességét méred (0.5ns).
        for (size_t i = 0; i < 32; ++i) {
            auto* f = pool->make_raw();
            if (f) {
                f->payload_length = 64;
                conduit->push(f);
            }
        }
        egress.poll();
    }
    state.SetItemsProcessed(state.iterations() * 32); // Each poll processes up to 32 items
}
BENCHMARK(BM_shm_egress_throughput);

/**
 * @brief Benchmarks the shm_arena_duplex offset translation.
 * Measures the overhead of pointer-to-offset reduction for relocatable memory.
 */
static void BM_shm_arena_translation(benchmark::State& state) {
    slabflux::core::hardware_topology::pin_thread(1);

    auto fake_arena = std::make_unique<std::vector<uint8_t>>(1024 * 1024 * 10); // 10MB arena
    auto arena_duplex = std::make_unique<shm_arena_duplex<1024>>("bench_arena", true, fake_arena->data());
    auto conduit = std::make_unique<core::spsc_conduit<core::tagged_pointer, 1024>>();

    for (auto _ : state) {
        if (conduit->occupancy() == 0) {
            void* ptr = fake_arena->data() + 1024;
            conduit->push(core::tagged_pointer::pack(1, ptr));
        }
        arena_duplex->process_egress_burst(*conduit);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_shm_arena_translation);

/**
 * @brief Benchmarks the shm_journal_duplex performance.
 * Validates the speed of arena-relative address transcoding.
 */
static void BM_shm_journal_duplex_throughput(benchmark::State& state) {
    slabflux::core::hardware_topology::pin_thread(1);

    auto fake_arena = std::make_unique<std::vector<uint8_t>>(1024 * 1024 * 10); // 10MB arena
    auto duplex = std::make_unique<shm_journal_duplex<1024>>("bench_journal", true, fake_arena->data());
    auto conduit = std::make_unique<core::spsc_conduit<core::tagged_pointer, 1024>>();

    for (auto _ : state) {
        if (conduit->occupancy() == 0) {
            // Simulate pointer within arena residency
            void* ptr = fake_arena->data() + 1024;
            conduit->push(core::tagged_pointer::pack(1, ptr));
        }
        duplex->process_egress_burst(*conduit);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_shm_journal_duplex_throughput);

/**
 * @brief Benchmarks the shm_inline_duplex with AVX-512 non-temporal stores.
 * Measures raw bandwidth of inlining small payloads (<256 bytes) directly into the ring.
 */
static void BM_shm_inline_nt_stores(benchmark::State& state) {
    slabflux::core::hardware_topology::pin_thread(1);

    auto inline_duplex = std::make_unique<shm_inline_duplex<1024, 128>>("bench_inline", true);
    auto conduit = std::make_unique<core::spsc_conduit<core::tagged_pointer, 1024>>();
    auto pool = std::make_unique<core::pinned_allocator_spsc<transport::raw_tcp_frame, 1024>>();

    for (auto _ : state) {
        // BURST REPLENISHMENT: Ne 1 elemet küldjünk, mert az megöli a pipeline-t
        for (size_t i = 0; i < 32; ++i) {
            auto* f = pool->make_raw();
            if (f) {
                f->payload_length = 64;
                conduit->push(core::tagged_pointer::pack(1, f));
            }
        }
        inline_duplex->process_egress_burst(*conduit);
    }
    state.SetItemsProcessed(state.iterations() * 32);
}

#if defined(__AVX512F__)
BENCHMARK(BM_shm_inline_nt_stores);
#endif

BENCHMARK_MAIN();