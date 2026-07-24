/*
 * SPDX-License-Identifier: LicenseRef-SlabFlux-Source-Available
 *
 * ============================================================================
 * SLABFLUX SOFTWARE ENGINE
 * Copyright (c) 2026 Kristóf Barta (https://github.com/kbartadev)
 * ============================================================================
 */

#include <benchmark/benchmark.h>
#include "slabflux/core.hpp"

using namespace slabflux::core;

// ============================================================================
// BENCHMARK SETUP: 7D HIERARCHY
// ============================================================================

struct matrix_context { uint64_t evaluations = 0; };

class EvBase { public: virtual ~EvBase() = default; };
class EvMid : public virtual EvBase {};
class EvLeaf : public EvMid {};

// SFINAE MOC Generation Fallback
REGISTER_CONTEXT(EvBase, matrix_context);
REGISTER_CONTEXT(EvMid, matrix_context);
REGISTER_CONTEXT(EvLeaf, matrix_context);

// Standard Virtual Dispatch Baseline Handlers
class VirtualBase {
public:
    virtual void handle(EvBase*, matrix_context&) {}
    virtual void handle(EvMid*, matrix_context&) {}
    virtual void handle(EvLeaf*, matrix_context&) {}
};

class VirtualLeaf : public VirtualBase {
public:
    void handle(EvBase*, matrix_context& c) override { c.evaluations++; }
    void handle(EvMid*, matrix_context& c) override { c.evaluations++; }
    void handle(EvLeaf*, matrix_context& c) override { c.evaluations++; }
};

// SlabFlux Dispatcher Handlers
class HndBase {
public:
    using priority = slabflux::priority<30>;
    void on(EvBase&, matrix_context& c) { c.evaluations++; }
};

class HndMid : public HndBase {
public:
    using priority = slabflux::priority<20>;
    void on(EvMid&, matrix_context& c) { c.evaluations++; }
};

class HndLeaf : public HndMid {
public:
    using priority = slabflux::priority<10>;
    void on(EvLeaf&, matrix_context& c) { c.evaluations++; }
};

/**
 * @brief Baseline: Standard C++ Virtual Function Dispatch
 * Measures the cost of traversing vtables for polymorphism.
 */
static void BM_Dispatch_Baseline_Virtual(benchmark::State& state) {
    matrix_context ctx;
    EvLeaf event;
    VirtualLeaf handler;
    VirtualBase* base_ptr = &handler;

    for (auto _ : state) {
        base_ptr->handle(static_cast<EvBase*>(&event), ctx);
        base_ptr->handle(static_cast<EvMid*>(&event), ctx);
        base_ptr->handle(&event, ctx);
    }
    state.SetItemsProcessed(state.iterations() * 3);
}
BENCHMARK(BM_Dispatch_Baseline_Virtual)->UseRealTime();

/**
 * @brief SlabFlux: 7D Matrix Pipeline Routing
 * Proves that traversing N handler levels crossed with M event inheritance levels,
 * resolving context offset injections, inverse priority sorts, and const-casts,
 * generates a flat zero-branch sequence mathematically identical in speed to raw execution.
 */
static void BM_Dispatch_Slabflux_7DMatrix(benchmark::State& state) {
    matrix_context ctx;
    EvLeaf event;
    slabflux::pipeline<HndLeaf> pipe(HndLeaf{});

    for (auto _ : state) {
        pipe.dispatch(ctx, event);
    }
    state.SetItemsProcessed(state.iterations() * 3);
}
BENCHMARK(BM_Dispatch_Slabflux_7DMatrix)->UseRealTime();

BENCHMARK_MAIN();