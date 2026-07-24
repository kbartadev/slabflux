# SlabFlux Core: Vector Lane Engine (`vector_lane_engine.hpp`, `kernels.hpp`)

## 1. Architectural Overview
The `vector_lane_engine` is the computational heavy artillery of the framework. It radically changes the execution paradigm by replacing scalar, branching arrays with unified, branchless processor loops driven purely by hardware SIMD (Single Instruction, Multiple Data) intrinsics.

## 2. Static Lane Geometry
The engine mathematically guarantees hardware parallelism by executing operations across grouped datasets rather than single elements.
- **Compile-Time Widths**: By statically defining the lane capacity via templates (e.g., `<64>` for AVX-512, `<32>` for AVX2), the engine forces the compiler to emit flawlessly optimized contiguous machine code.
- **Zero-Branch Iteration**: It leverages aggressive loop unrolling directives (`#pragma GCC unroll 4` on GCC/Clang) to physically flatten the computation steps, eliminating the risk of branch-prediction cache misses along the critical hot path.

## 3. Reactor Pattern & Kernel Fusion
The `physics_reactor` subcomponent ingests high-frequency temporal streams (e.g., `tick_event`) directly from the L1 cache and broadcasts vectorized algorithmic transformations across all available hardware lanes simultaneously.

### Metaprogrammed Execution Graphs (`kernels.hpp`)
Complex mathematical formulas are abstracted behind the `execution_graph<...Ops>` composition engine.
- It fuses multiple nodes (like exponential decay or ReLUs) into a single functional block.
- **FMA Streamlining**: It reduces complex scaling chains into pure Fused Multiply-Add instructions (`_mm512_fmadd_ps`). 
- This ensures that the state math remains entirely within the ZMM/YMM register files for the entire computation chain before finally storing the results back to the main memory slab, saving dozens of CPU clock cycles per frame.