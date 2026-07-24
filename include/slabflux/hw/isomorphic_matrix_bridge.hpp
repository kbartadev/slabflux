#pragma once

#include <cstdint>
#include <cstddef>
#include <immintrin.h>
#include <atomic>
#include <bit>
#include <new> // std::hardware_constructive_interference_size
#include "slabflux/core/hot_path_alignment.hpp"

namespace slabflux::gpu {

inline constexpr std::size_t MATRIX_LANES = 128; // Number of supported GPU SMs
static_assert(std::has_single_bit(MATRIX_LANES), "MATRIX_LANES must be a power of two for O(1) bitmasking.");

// Emission Slot: CPU writes, GPU reads
// Pure Standard-Layout C++ struct for PCIe WCB streaming.
struct alignas(CACHE_LINE_SIZE) emission_slot {
    uint64_t tensor_a_ptr;
    uint64_t tensor_b_ptr;
    uint64_t result_ptr;
    uint32_t dimension_x;
    uint32_t dimension_y;
    uint32_t operation_code;
    uint32_t reserved_padding[3];
    uint32_t phase_tag; // Synchronization phase
};

// Guarantees that the PCIe transaction tiles perfectly as a single TLP packet over the Cache line.
static_assert(sizeof(emission_slot) == 64, "Emission slot must tile perfectly to 64 bytes.");

// Response Slot: GPU writes, CPU Ingress reads
struct alignas(CACHE_LINE_SIZE) response_slot {
    uint64_t result_ptr;
    uint64_t integrity_hash;
    uint64_t reserved_padding[5];
    uint32_t phase_tag;
};

static_assert(sizeof(response_slot) == 64, "Response slot must tile perfectly to 64 bytes.");

/**
 * @class isomorphic_matrix_bridge
 * Zero-Blocking GPU bridge used by deterministic CPU core.
 */
class isomorphic_matrix_bridge {
    emission_slot* emission_plane_;
    uint32_t current_lane_{0};
    uint32_t current_phase_{1};

public:
    explicit isomorphic_matrix_bridge(emission_slot* mapped_pcie_memory) noexcept 
        : emission_plane_(mapped_pcie_memory) {}

    // Specifically branch-minimal dispatch callable from deterministic threads
    void stream_evaluation_state(uint64_t t_a, uint64_t t_b, uint64_t res, 
                                 uint32_t dx, uint32_t dy, uint32_t op) noexcept {
        
        // 1. Create data structure in local L1 stack
        alignas(CACHE_LINE_SIZE) emission_slot slot{};
        slot.tensor_a_ptr = t_a;
        slot.tensor_b_ptr = t_b;
        slot.result_ptr = res;
        slot.dimension_x = dx;
        slot.dimension_y = dy;
        slot.operation_code = op;
        slot.phase_tag = current_phase_;

        // 2. Simulate Modulo operator with bitmask (O(1) Branchless)
        uint32_t target_lane = current_lane_;
        current_lane_ = (current_lane_ + 1) & (MATRIX_LANES - 1);
        
        if (current_lane_ == 0) [[unlikely]] { // Wrap-around handling branch predictor hint
            current_phase_ = (current_phase_ + 1) & 0xFF;
            if (current_phase_ == 0) current_phase_ = 1; // 0 is VACUUM state
        }

        // 3. AVX-512 Non-Temporal memory movement to PCIe WCB (L1 Cache Bypass)
        __m512i data = _mm512_load_si512(&slot);
        _mm512_stream_si512(&emission_plane_[target_lane], data);
        
        // 4. Memory serialization: guarantees PCIe hardware flush
        _mm_sfence();
    }
};

/**
 * @class gpu_ingress_poller
 * Runs on a completely isolated OS thread, feeding back results to the deterministic core.
 */
template <typename IngressConduit>
class gpu_ingress_poller {
    response_slot* response_plane_;
    IngressConduit* deterministic_conduit_;
    uint32_t expected_phase_[MATRIX_LANES];

public:
    gpu_ingress_poller(response_slot* mapped_response_memory, IngressConduit* conduit) noexcept
        : response_plane_(mapped_response_memory), deterministic_conduit_(conduit) {
        for (uint32_t i = 0; i < MATRIX_LANES; ++i) {
            expected_phase_[i] = 1;
        }
    }

    // The separate thread calls this function in an infinite loop
    void poll_sweep() noexcept {
        #pragma GCC unroll 8
        for (uint32_t lane = 0; lane < MATRIX_LANES; ++lane) {
            // TSO model O(1) read without synchronization overhead (Acquire Barrier)
#if defined(__GNUC__) || defined(__clang__)
            uint32_t tag = __atomic_load_n(&response_plane_[lane].phase_tag, __ATOMIC_ACQUIRE);
#else
            uint32_t tag = *static_cast<volatile uint32_t*>(&response_plane_[lane].phase_tag);
            std::atomic_thread_fence(std::memory_order_acquire);
#endif
            
            if (tag == expected_phase_[lane]) {
                _mm_lfence();
                
                uint64_t completed_result_ptr = response_plane_[lane].result_ptr;
                
                // Asynchronous, Zero-Wait handoff for the deterministic CPU core
                deterministic_conduit_->try_push(completed_result_ptr);
                
                expected_phase_[lane] = (expected_phase_[lane] + 1) & 0xFF;
                if (expected_phase_[lane] == 0) [[unlikely]] expected_phase_[lane] = 1;
            }
        }
    }
};

} // namespace slabflux::gpu