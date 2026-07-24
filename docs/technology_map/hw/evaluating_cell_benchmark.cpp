#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include "slabflux/hw/evaluating_cell.hpp"

namespace slabflux::hw::benchmark {

// Hardware FMA (Fused Multiply-Add) simulation for performance testing
struct benchmark_math_functor {
    static void compute(const float* a, const float* b, float* out) noexcept {
        // GCC unroll guarantees ALU vectorization (SIMD)
        #pragma GCC unroll 8
        for (int i = 0; i < 30; ++i) {
            out[i] = a[i] * b[i] + 0.5f; 
        }
    }
};

class EvaluatingCellBenchmark : public ::testing::Test {
protected:
    // Strict physical geometry aligned to L2 Cache sectors (Zero False Sharing)
    alignas(GPU_CACHE_SECTOR_SIZE) spatial_register reg_in_a_{};
    alignas(GPU_CACHE_SECTOR_SIZE) spatial_register reg_in_b_{};
    alignas(GPU_CACHE_SECTOR_SIZE) spatial_register reg_out_{};

    void SetUp() override {
        std::memset(&reg_in_a_, 0, sizeof(spatial_register));
        std::memset(&reg_in_b_, 0, sizeof(spatial_register));
        std::memset(&reg_out_, 0, sizeof(spatial_register));
        
        // Loading dummy payload
        for (int i = 0; i < 30; ++i) {
            reg_in_a_.payload_matrix[i] = 1.0f;
            reg_in_b_.payload_matrix[i] = 2.0f;
        }
    }
};

TEST_F(EvaluatingCellBenchmark, MeasureThroughput) {
    evaluating_cell<benchmark_math_functor> cell(&reg_in_a_, &reg_in_b_, &reg_out_);

    // Simulating GPU "Persistent Thread" on a dedicated OS thread
    std::thread alu_thread([&cell]() {
        cell.continuous_evaluation_loop();
    });
    
    alu_thread.detach();

    constexpr uint64_t ITERATIONS = 10'000'000;
    uint32_t current_phase = 1;

    // Warmup for the hardware Branch Predictor and L1/L2 Cache
    for (int i = 0; i < 1000; ++i) {
        __atomic_store_n(&reg_in_a_.phase_tag, current_phase, __ATOMIC_RELEASE);
        __atomic_store_n(&reg_in_b_.phase_tag, current_phase, __ATOMIC_RELEASE);

        while (__atomic_load_n(&reg_out_.phase_tag, __ATOMIC_ACQUIRE) != current_phase) {
            asm volatile("pause" ::: "memory");
        }
        current_phase = (current_phase + 1) & 0xFF;
        if (current_phase == 0) [[unlikely]] current_phase = 1;
    }

    // --- Benchmark Start ---
    auto start_time = std::chrono::high_resolution_clock::now();

    for (uint64_t i = 0; i < ITERATIONS; ++i) {
        // Systolic Push (Data-Driven Trigger)
        __atomic_store_n(&reg_in_a_.phase_tag, current_phase, __ATOMIC_RELEASE);
        __atomic_store_n(&reg_in_b_.phase_tag, current_phase, __ATOMIC_RELEASE);

        // Zero-Overhead Polling on the output
        while (__atomic_load_n(&reg_out_.phase_tag, __ATOMIC_ACQUIRE) != current_phase) {
            asm volatile("pause" ::: "memory");
        }

        current_phase = (current_phase + 1) & 0xFF;
        if (current_phase == 0) [[unlikely]] current_phase = 1;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    // --- Benchmark End ---

    std::chrono::duration<double> elapsed = end_time - start_time;
    double ops_per_sec = ITERATIONS / elapsed.count();
    double latency_ns = (elapsed.count() * 1e9) / ITERATIONS;

    std::cout << "[----------] Benchmark Results:\n";
    std::cout << "[   INFO   ] Iterations : " << ITERATIONS << "\n";
    std::cout << "[   INFO   ] Elapsed    : " << std::fixed << std::setprecision(4) << elapsed.count() << " seconds\n";
    std::cout << "[   INFO   ] Throughput : " << std::fixed << std::setprecision(0) << ops_per_sec << " ops/sec\n";
    std::cout << "[   INFO   ] Latency    : " << std::fixed << std::setprecision(2) << latency_ns << " ns/op\n";

    // Security check: Throughput within a single CPU must be in the order of millions
    EXPECT_GT(ops_per_sec, 1'000'000) << "Throughput abnormally low; check thermal throttling or false sharing.";
}

} // namespace slabflux::hw::benchmark