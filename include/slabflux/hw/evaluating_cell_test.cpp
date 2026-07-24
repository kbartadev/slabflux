#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cstring>
#include "slabflux/hw/evaluating_cell.hpp"

namespace slabflux::hw::test {

// Simulated mathematical role (MathFunctor) for the cell: Simple vector addition
struct tensor_add_functor {
    static void compute(const float* a, const float* b, float* out) noexcept {
        // Deliberately simple, verifiable deterministic operation
        #pragma GCC unroll 4
        for (int i = 0; i < 30; ++i) {
            out[i] = a[i] + b[i];
        }
    }
};

class EvaluatingCellTest : public ::testing::Test {
protected:
    // Default registers aligned to 128-byte L2 cache lines
    alignas(GPU_CACHE_SECTOR_SIZE) spatial_register reg_in_a_{};
    alignas(GPU_CACHE_SECTOR_SIZE) spatial_register reg_in_b_{};
    alignas(GPU_CACHE_SECTOR_SIZE) spatial_register reg_out_{};

    void SetUp() override {
        std::memset(&reg_in_a_, 0, sizeof(spatial_register));
        std::memset(&reg_in_b_, 0, sizeof(spatial_register));
        std::memset(&reg_out_, 0, sizeof(spatial_register));
    }
};

TEST_F(EvaluatingCellTest, SystolicDataflowExecutionAndCleanShutdown) {
    // 1. Cell instantiation with shared physical memory registers
    evaluating_cell<tensor_add_functor> cell(&reg_in_a_, &reg_in_b_, &reg_out_);

    // 2. Starting simulated GPU "Persistent Thread" on a dedicated OS thread
    std::thread alu_thread([&cell]() {
        cell.continuous_evaluation_loop();
    });

    // --- PHASE ONE (Phase 1) ---
    
    for (int i = 0; i < 30; ++i) {
        reg_in_a_.payload_matrix[i] = static_cast<float>(i);
        reg_in_b_.payload_matrix[i] = 100.0f;
    }

    // Data-driven Trigger: Simultaneous publication of phases (Release semantics)
    __atomic_store_n(&reg_in_a_.phase_tag, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&reg_in_b_.phase_tag, 1, __ATOMIC_RELEASE);

    // Polling on the output (simulated Host/Next Cell read)
    uint32_t out_phase = 0;
    while ((out_phase = __atomic_load_n(&reg_out_.phase_tag, __ATOMIC_ACQUIRE)) != 1) {
        asm volatile("pause" ::: "memory"); // x86 pause a tesztszál CPU kímélésére
    }

    EXPECT_EQ(out_phase, 1);
    EXPECT_EQ(reg_out_.diagnostic_hash, 1 ^ 0xCAFEBABE);
    for (int i = 0; i < 30; ++i) {
        EXPECT_FLOAT_EQ(reg_out_.payload_matrix[i], static_cast<float>(i) + 100.0f);
    }

    // --- PHASE TWO (Phase 2) ---

    for (int i = 0; i < 30; ++i) {
        reg_in_a_.payload_matrix[i] = 5.0f;
        reg_in_b_.payload_matrix[i] = 5.0f;
    }

    __atomic_store_n(&reg_in_a_.phase_tag, 2, __ATOMIC_RELEASE);
    __atomic_store_n(&reg_in_b_.phase_tag, 2, __ATOMIC_RELEASE);

    while ((out_phase = __atomic_load_n(&reg_out_.phase_tag, __ATOMIC_ACQUIRE)) != 2) {
        asm volatile("pause" ::: "memory");
    }

    EXPECT_EQ(out_phase, 2);
    for (int i = 0; i < 30; ++i) {
        EXPECT_FLOAT_EQ(reg_out_.payload_matrix[i], 10.0f);
    }

    // --- CLEAN SHUTDOWN (KILL PHASE) ---
    __atomic_store_n(&reg_in_a_.phase_tag, PHASE_KILL, __ATOMIC_RELEASE);
    __atomic_store_n(&reg_in_b_.phase_tag, PHASE_KILL, __ATOMIC_RELEASE);
    
    if (alu_thread.joinable()) alu_thread.join(); // Successful Thread Join!
}

} // namespace slabflux::hw::test