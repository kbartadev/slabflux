# SlabFlux AI: Deterministic AI Core (`deterministic_ai_core.hpp`)

## 1. Architectural Overview
The `deterministic_ai_core` is a zero-branching, O(1) SIMD-based inference engine. It executes neural network and machine learning updates synchronously within the deterministic mesh, enforcing absolute cross-node parity.

## 2. Invariant Activation Policies
The engine eliminates `libm` dependencies (which vary across platforms) by using `constexpr`-ready, branchless approximations of transcendental functions, such as the 3rd-order minimax Sigmoid approximation.

## 3. Heterogeneous Tiered Offload
The core evaluates the tensor `CAPACITY` at compile-time:
- **CPU Mesh**: For smaller tensors, it unrolls into AVX-512 or AVX2 `_mm512_fmadd_ps` instruction streams.
- **GPU Offload**: For massive state updates (e.g., > 1 million parameters), the compiler natively routes the execution through the `gpu_accelerator` boundary, invoking asynchronous CUDA/HIP kernels without blocking the CPU hot-path.

## 4. Numerical Sanitization
To prevent NaN or Infinity values from destroying the state matrix, the core uses the `numerical_sanitizer`. It performs differential cleansing using bit-masks, replacing poisoned tensor lanes with a configured baseline or a neighbor-weighted interpolation, entirely avoiding scalar `if/else` checks.