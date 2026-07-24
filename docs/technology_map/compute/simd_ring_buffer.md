# SlabFlux Compute: SIMD Ring Buffer (`simd_ring_buffer.hpp`)

## 1. Architectural Overview
Evaluating moving averages (EMA/SMA), volume profiles, or historical physics trajectories requires analyzing a sliding window of recent data. Iterating through a standard array element-by-element destroys the cycle budget.
The `simd_ring_buffer` provides a statically-sized, AVX-aligned history window designed exclusively for vectorized bulk processing.

## 2. Unrolled Matrix Evaluation
The ring buffer forces the underlying storage into a contiguous `alignas(64)` array of multiples of 8 or 16 elements.
- When the `physics_reactor` requests the average of the last 32 ticks, the ring buffer returns a directly castable `__m512` pointer.
- The CPU evaluates all 32 historical elements in just two clock cycles using `_mm512_add_ps` and `_mm512_mul_ps`, completely avoiding scalar iteration.

## 3. Hardware Masking for Wrap-Around
Handling circular wrap-around during a vectorized read is mathematically complex. 
The `simd_ring_buffer` resolves this using **Hardware Masking**:
- It generates a `__mmask16` representing the boundary of the ring.
- It executes a masked SIMD load (`_mm512_mask_loadu_ps`) for the end of the array, and a complementary masked load for the beginning of the array.
- It blends the two registers seamlessly, providing the mathematical engine with a perfectly linearized historical vector without a single CPU branch.