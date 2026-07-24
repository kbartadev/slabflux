# SlabFlux Logic: Logic Manifold (`logic.hpp`)

## 1. Architectural Overview
The `slabflux::logic` namespace enforces structural bounds against the inclusion of any non-deterministic C++ constructs. It bridges the gap between hardware primitives and domain-specific Trading/AI rulesets.

## 2. The Logic Expert Concept
Replacing legacy abstract classes and virtual pointers (V-Tables), the `logic_expert` is a C++20 `concept`.
It requires any domain class to implement the exact signature `on_event(const EventType* ev, uint64_t lsn) noexcept`. This forces all domain interactions to be evaluated at compile-time, resulting in absolute inlining and zero-cost dispatch.

## 3. Masked Predication (Blending)
Branching on volatile market data creates massive instruction cache jitter due to branch mispredictions.
The `logic::blend` method completely replaces standard `if/else` conditions. The compiler lowers this abstraction directly to Conditional Move (`CMOV`) or bit-masking operations, guaranteeing that the execution path length is identical regardless of the payload's state.

## 4. Structural Isolation
The `logic_manifold` manages the internal state arrays for logic nodes.
- Enforces `alignas(64)` padding and requires the Capacity to be a multiple of 8, preparing the payload directly for SIMD striding.
- Replaces standard bounds checking (`std::vector::at`) with bitmask clamping (`idx & (Capacity - 1)`), providing absolute `O(1)` memory access without risking branch-penalty faults.