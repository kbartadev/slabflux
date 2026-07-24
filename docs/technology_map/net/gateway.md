# SlabFlux Net: Generic Gateway (`slabflux/net/gateway.hpp`)

## 1. Architectural Justification
Provides the foundational contract for network routing nodes, establishing strict memory and execution bounds that all protocol-specific gateways (e.g., FIX, FAST, ITCH) must implement.

## 2. Hardware Implementation Directives
- **Branchless Interface**: Enforces that all inheriting or templated gateways avoid virtual functions (`vptr`), utilizing Curiously Recurring Template Pattern (CRTP) or `constexpr` dispatch to maintain L1 instruction cache coherency.
- **Memory Alignment**: Forces all routing arrays and jump-tables to conform to `alignas(64)` standards to prevent False Sharing with neighboring execution threads.

## 3. Pipeline Integration
Acts as the architectural blueprint for the topological mesh. It guarantees that any custom routing logic injected into the pipeline conforms to the strict zero-allocation, wait-free execution guarantees required by the `branchless_engine`.