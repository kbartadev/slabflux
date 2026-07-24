# SlabFlux Compute: Divergence Analyzer (`divergence_analyzer.hpp`)

## 1. Architectural Overview
The `divergence_analyzer` guarantees bit-exact execution parity across the distributed mesh. It compares the active engine state against a known-good reference state (or snapshot) to identify specific SIMD lanes that have drifted.

## 2. Drift Policies
The analyzer supports multiple hardware-accelerated comparison policies:
- **BIT_IDENTICAL**: Reports any non-zero difference.
- **MSE_BASED**: Computes Mean Squared Error (MSE) and reports if it exceeds a dynamic threshold.
- **PSNR_BASED**: Computes Peak Signal-to-Noise Ratio (PSNR) to measure structural signal degradation.

## 3. AVX-512 Horizontal Reduction
State comparisons avoid scalar loops. The analyzer uses AVX-512 `_mm512_sub_ps` and `_mm512_mask_fmadd_ps` to compute squared errors across 16 lanes simultaneously, followed by a horizontal reduction (`_mm512_reduce_add_ps`). It masks out NaN/Inf anomalies to prevent poisoned data from skewing the MSE calculation.

## 4. Zero-Syscall Telemetry
If a divergence is detected, the analyzer does not call `std::cerr` or `syslog`. Instead, it uses **Kinetic Inscription** to engrave the anomaly (Error Code `0x0D` and the LSN) directly into the CPU's Last Branch Record (LBR) MSR. This ensures that detecting an error never stalls the critical path.