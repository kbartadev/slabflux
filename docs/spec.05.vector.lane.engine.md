# Vector Lane Engine (SIMD)

The `slabflux::compute::vector_lane_engine` serves as the computational heavy artillery of the framework, exploiting the CPU�s SIMD (Single Instruction, Multiple Data) execution units for massive bulk data processing.

## Core Principles
* **Hardware Parallelism:** Operations are not executed sequentially. In a single clock cycle, the engine computes 8, 16, or 32 discrete data elements simultaneously, dictated by the target lane width.
* **Compile-Time Fixed Width:** By statically defining the `LaneSize` via templates (e.g., `<64>` for AVX-512, `<32>` for AVX2), the compiler is forced to emit flawlessly optimized, contiguous machine code.
* **Zero-Branch Iteration:** Aggressive loop unrolling (via `#pragma GCC unroll`) effectively eliminates branch-prediction cache misses along the critical hot path.

## Key Components
1. **`vector_lane_512.hpp`:** A bespoke implementation targeting Intel�s AVX-512 register sets. Natively supports hardware-masked loads and parallelized floating-point propagation.
2. **`vector_lane_256<Size>`:** A generalized high-level interface unifying underlying lane architectures. Capable of ingesting sequences of temporal events (e.g., `tick_event`) directly from L1 cache and fusing them into singular vectorized computations.

## Performance Optimization (MSVC vs GCC)
Because MSVC compilers silently ignore the `#pragma GCC unroll` directive, conditional compilation guards are enforced to prevent build failures while safeguarding absolute peak performance on Linux bare-metal:

```cpp
#ifndef _MSC_VER
    #pragma GCC unroll 4
#endif
```

## `slabflux::compute::physics_reactor`
An advanced, SIMD-accelerated processing node engineered to evaluate complex mathematical topologies and physical models in parallel.
* **Reactor Pattern:** Ingests high-frequency `tick_event` streams and broadcasts vectorized algorithmic transformations across all available hardware lanes.
* **Temporal Guard:** Enforces strict execution bounds, ensuring the reactor mathematically processes data only within a legitimate `timing_invariant` window.

## `slabflux::compute::long_entropy_rng`
## `slabflux::compute::deterministic_rng`
