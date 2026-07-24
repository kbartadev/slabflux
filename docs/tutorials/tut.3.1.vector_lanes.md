# Tutorial 3.1: Branchless Vector Lanes

## 1. The SIMD Imperative
Standard scalar loops (`for (int i = 0; i < N; ++i)`) iterating over arrays of data incur heavy branch prediction costs and underutilize modern CPU silicon. In high-frequency environments, applying transformations to order books or telemetry arrays must be done using Single Instruction, Multiple Data (SIMD) paradigms.

SlabFlux exposes hardware-specific vector processing through the `compute/` subsystem, explicitly forbidding `if/else` branches within the lane execution.

## 2. Vector Lane Engines (`vector_lane_512.hpp` & `vector_lane_engine.hpp`)
The `vector_lane_engine` orchestrates 512-bit wide registers, processing 16 floats simultaneously. It mandates **L1 Cache Sector Alignment** (`alignas(64)`) to ensure that `_mm512_load_ps` operations never cross cache-line boundaries, which would trigger hardware penalty cycles.

## 3. Branchless Execution via Bitmasks
When logic requires a conditional branch (e.g., "if price > threshold, update volume"), AVX-512 prevents control flow divergence via **mask registers** (`__mmask16`). You execute the math for *all* elements, but only blend the results back into the register where the mask bit is `1`.

### Hands-On: SIMD Pricing Transformation

```cpp
#include "slabflux/compute/vector_lane_engine.hpp"
#include "slabflux/compute/vector_lane_512.hpp"
#include <immintrin.h>
#include <iostream>

struct alignas(64) PriceArray {
    float prices[16];
    float thresholds[16];
    float outputs[16];
};

int main() {
    PriceArray data;
    // Assume data.prices and data.thresholds are populated...
    for (int i=0; i<16; ++i) {
        data.prices[i] = 100.0f + i;
        data.thresholds[i] = 105.0f;
        data.outputs[i] = 0.0f;
    }

    // 1. Load 16 floats into AVX-512 registers directly
    __m512 v_prices = _mm512_load_ps(data.prices);
    __m512 v_thresholds = _mm512_load_ps(data.thresholds);

    // 2. Vectorized, branchless comparison. Returns a 16-bit mask.
    // 1 if price > threshold, 0 otherwise.
    __mmask16 cmp_mask = _mm512_cmp_ps_mask(v_prices, v_thresholds, _CMP_GT_OQ);

    // 3. Compute a transformation (e.g., apply a 5% markup to ALL prices)
    __m512 v_markup = _mm512_set1_ps(1.05f);
    __m512 v_new_prices = _mm512_mul_ps(v_prices, v_markup);

    // 4. Blend the results branchlessly using the mask
    // If mask is 1: use v_new_prices. If mask is 0: keep original v_prices.
    __m512 v_final = _mm512_mask_blend_ps(cmp_mask, v_prices, v_new_prices);

    // 5. Store exactly 64 bytes back to memory
    _mm512_store_ps(data.outputs, v_final);
    
    return 0;
}
```

## 4. Best Practices
*   **Alignment is Law:** AVX-512 operations will instantly segfault if pointers are not 64-byte aligned.
*   **Avoid Mixed Widths**: Do not mix AVX-256 and AVX-512 in the same hot-path loop; this can trigger "AVX-SSE transitions" which downclock the CPU core frequencies.