# Blueprint: Core Compute Architecture

## Architectural Overview
Computation strictly prohibits `vtable` execution. The framework embeds advanced metaprogramming logic to construct a unified static computation Directed Acyclic Graph (DAG) during the compilation phase, enabling mathematical inlining of behavior execution.

## Core Components
- **Cartesian DAG Dispatcher (`pipeline.hpp`, `dispatcher.hpp`)**: Utilizes C++ SFINAE type-introspection (`exact_event_match`) and zero-overhead topological sorting (`true_topological_sort`) to seamlessly orchestrate dependent events and unroll handler tuples iteratively.
- **Duck-Typing & Concepts Recognition**: Handlers map dynamically to arbitrary memory structures purely based on expected characteristics (e.g. `HasVelocity`) ensuring data structs stay free of base-class pointers.
- **Vector Lane Engines (`vector_lane_engine.hpp`, `simd_engine.hpp`)**: Decouples scalar loops, broadcasting algorithmic formulas to arrays of logical entities simultaneously utilizing intrinsic sets (e.g., AVX2 and AVX-512 `_mm512_loadu_si512`).
- **Type Demultiplexing (`demuxer.hpp`)**: Dispatches `tagged_pointer` envelopes immediately. Uses compile-time resolved C++17 fold-expressions mapping type IDs into jump-table execution paths dynamically.
- **Sovereign Graph Physics (`causal_entity.hpp`, `entity_slab.hpp`)**: Establishes continuous Structure-of-Array (SoA) layouts for entities mapped across 3D dimensions and monotonic time (`__rdtsc()`), tracking causal dependencies internally via parent/child identity graphs.