# SlabFlux Pipeline: Phase Engine (`slabflux/pipeline/phase_engine.hpp`)

## 1. Architectural Justification
Trading systems operate in distinct temporal phases (e.g., Pre-Market, Continuous Trading). Routing events to inactive handlers destroys cache coherency. The `phase_engine` statically filters handlers based on their registered phase constraints.

## 2. Compile-Time Implementation Directives
- **Exclusivity Guards**: Evaluates `phase_tag` and signature-level phase parameters. Fails the compilation (`static_assert`) if a handler violates mutual exclusivity rules, ensuring mathematically impossible states never reach the binary.
- **Zero-Cost Filtering**: The phase validation evaluates to `constexpr bool`. If a handler is not active in the requested phase, its invocation code is completely removed from the compiled assembly.

## 3. Pipeline Integration
Plugs into the `dispatch_unroller` as a pre-invocation filter. Handlers not belonging to the currently active phase are entirely stripped from the unrolled execution path, guaranteeing absolute zero overhead for inactive logic.