#pragma once

#include <cstdint>
#include <cstddef>

namespace slabflux::hw {

// Special phase tag for clean shutdown of the infinite loop
inline constexpr uint32_t PHASE_KILL = 0xFF;

// Hardware invariant: Ideal alignment of GPU L2 cache sectors
inline constexpr std::size_t GPU_CACHE_SECTOR_SIZE = 128;

/**
 * @struct spatial_register
 * Static memory cell located in GPU VRAM. 
 * Perfectly fills a single cache line (Zero False Sharing).
 */
struct alignas(GPU_CACHE_SECTOR_SIZE) spatial_register {
    float payload_matrix[30];    // 120 bytes of computational data
    uint32_t diagnostic_hash;    // 4 bytes of telemetry state
    uint32_t phase_tag;          // 4 bytes of synchronization phase
};

static_assert(sizeof(spatial_register) == GPU_CACHE_SECTOR_SIZE, "Spatial register must tile exactly to the GPU cache sector.");

/**
 * @class evaluating_cell
 * Continuously evaluating state machine mapped to GPU core (ALU).
 */
template <typename MathFunctor>
class evaluating_cell {
    spatial_register* input_a_;
    spatial_register* input_b_;
    spatial_register* output_;
    uint32_t expected_phase_{1};

public:
    evaluating_cell(spatial_register* source_a, spatial_register* source_b, spatial_register* destination) noexcept
        : input_a_(source_a), input_b_(source_b), output_(destination) {}

    void continuous_evaluation_loop() noexcept {
        #pragma unroll 1 
        while (true) {
            // 1. Zero-abstraction C++ Atomic read (Acquire) - Eliminates std::atomic_ref overhead
#if defined(__GNUC__) || defined(__clang__)
            uint32_t tag_a = __atomic_load_n(&input_a_->phase_tag, __ATOMIC_ACQUIRE);
            uint32_t tag_b = __atomic_load_n(&input_b_->phase_tag, __ATOMIC_ACQUIRE);
#else
            uint32_t tag_a = *static_cast<volatile uint32_t*>(&input_a_->phase_tag);
            uint32_t tag_b = *static_cast<volatile uint32_t*>(&input_b_->phase_tag);
            asm volatile("":::"memory"); // Compiler barrier
#endif

            // Clean shutdown check
            if (tag_a == PHASE_KILL || tag_b == PHASE_KILL) [[unlikely]] {
                break;
            }

            if (tag_a == expected_phase_ && tag_b == expected_phase_) {
                
                MathFunctor::compute(input_a_->payload_matrix, 
                                     input_b_->payload_matrix, 
                                     output_->payload_matrix);

                output_->diagnostic_hash = expected_phase_ ^ 0xCAFEBABE;

                // 2. Zero-abstraction C++ Atomic write (Release)
#if defined(__GNUC__) || defined(__clang__)
                __atomic_store_n(&output_->phase_tag, expected_phase_, __ATOMIC_RELEASE);
#else
                asm volatile("":::"memory"); // Compiler barrier precedes the store
                *static_cast<volatile uint32_t*>(&output_->phase_tag) = expected_phase_;
#endif

                // The PHASE_KILL (0xFF) value is reserved, so it is skipped
                expected_phase_ = (expected_phase_ + 1) & 0xFF;
                if (expected_phase_ == 0 || expected_phase_ == PHASE_KILL) [[unlikely]] expected_phase_ = 1;
            } else {
                // 3. Conserving VRAM Bus via architecture-specific hardware pause (Busy-Wait mitigation)
#if defined(__AMDGCN__)
                asm volatile("s_sleep 1" ::: "memory");
#elif defined(__NVPTX__)
                asm volatile("nanosleep.u32 32;" ::: "memory");
#else
                asm volatile("":::"memory"); 
#endif
            }
        }
    }
};

} // namespace slabflux::hw