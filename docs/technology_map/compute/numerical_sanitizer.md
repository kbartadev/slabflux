# SlabFlux Compute: Numerical Sanitizer (`numerical_sanitizer.hpp`)

## 1. Architectural Overview
Deep neural logic and floating-point computations are susceptible to NaN (Not-a-Number) and Infinity poisoning, which can aggressively propagate and corrupt entire state matrices. The `numerical_sanitizer` intercepts and cleanses these values using zero-branching SIMD intrinsics.

## 2. Hardware Classification
Instead of calling `std::isnan()` iteratively, the sanitizer executes `_mm512_fpclass_ps_mask(v_data, 0x99)`.
This single instruction evaluates 16 floats simultaneously against the IEEE-754 bit-signatures for QNaN, SNaN, +Infinity, and -Infinity, emitting a consolidated 16-bit physical validity mask.

## 3. Template-Injected Cleansing Policies
When corrupted lanes are identified, the hardware executes a corrective Bit-Blend (`_mm512_mask_blend_ps`) based on the active policy:
- **Baseline Stabilizer**: Automatically substitutes the poisoned lane with a pre-configured neutral value (e.g., `0.0f`).
- **Neighbor Weighted**: Interpolates the target lane using values from its immediate left and right spatial neighbors (`_mm512_alignr_epi32`), effectively healing the tensor via structural osmosis.

## 4. Congestion Backpressure
If the hardware detects that more than 5% of the tensor lanes are poisoned during a single cycle, it dynamically elevates the severity of the `drift_policy` from Bit-Identical to MSE-Based, signaling the environment to preemptively trigger a state snapshot.