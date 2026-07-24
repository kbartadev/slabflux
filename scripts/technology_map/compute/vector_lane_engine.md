# SlabFlux Compute: Vector Lane Engine (`vector_lane_engine.hpp`)

## 1. Architectural Overview
The `vector_lane_engine` is a pure hardware-mapped mathematical topology engine. It is the primary structure responsible for unrolling Sequential Algorithmic Graphs (like AI matrix updates or quantitative pricing models) directly into parallel SIMD registers with absolute zero branching.

## 2. Compile-Time Metaprogramming
The engine accepts a `KernelGraph` template parameter. This graph represents a nested series of operations (`fma_op`, `relu_op`, `decay_op`).
- The C++ compiler physically unrolls the AST of the Kernel Graph into a completely flat execution block of AVX-512 FMA (`_mm512_fmadd_ps`) instructions.
- The data resides entirely inside the 512-bit ZMM CPU registers until the whole mathematical pipeline finishes, obliterating L1 cache roundtrips.

## 3. Instruction-Level Pipelining
To saturate the CPU execution ports, the engine utilizes `#pragma GCC unroll 4`.
- This forces the CPU to issue 4 independent `_mm512_load_ps` instructions simultaneously.
- By overlapping memory fetching with arithmetic execution across 64 separate floats per cycle, the engine achieves near-theoretical peak IPC (Instructions Per Clock).

## 4. State Matrix Sanitization
The state matrix (`memory_state_`) is strictly padded to 64 bytes. Resetting the state uses Non-Temporal (`_mm512_stream_ps`) zeroing, which physically clears the DRAM without thrashing the L1 cache, preserving execution context for the primary pipeline.