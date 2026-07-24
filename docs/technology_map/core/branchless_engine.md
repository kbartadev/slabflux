# SlabFlux Core: Branchless Engine (`slabflux/core/branchless_engine.hpp`)

## 1. Architectural Justification
The `branchless_engine` is the sovereign deterministic compute matrix of SlabFlux. It processes high-frequency trading logic and state transitions without incurring branch prediction misprediction penalties that plague traditional execution loops.

## 2. Hardware Implementation Directives
- **Predication over Branching**: Transforms conditional control flows into bitwise arithmetic (`cmov`, AVX-512 masking). All execution paths are processed concurrently, and only the mathematically valid result is committed to memory.
- **L1 Cache Residency**: The entire engine footprint, including state matrices and pricing vectors, is strictly compacted and aligned to never exceed the CPU's 32KB L1 Data Cache limits.
- **Wait-Free Topology**: Receives inputs exclusively from `spsc_conduit` structures. It executes purely as a consumer of inputs and a producer of outputs, immune to kernel preemption.

## 3. Execution Guarantee
By entirely eliminating `if/else` branching in the critical path, the engine guarantees mathematically flat O(1) latency regardless of the input data shape or structural complexity.