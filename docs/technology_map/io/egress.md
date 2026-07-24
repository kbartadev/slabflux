# SlabFlux I/O: Generic Egress (`slabflux/io/egress.hpp`)

## 1. Architectural Justification
Defines the foundational polymorphism-free interface and structural boundaries for all specialized transmission modules (e.g., DPDK, io_uring, Socket). It ensures all egress engines conform to the same strict zero-allocation rules.

## 2. Hardware Implementation Directives
- **CRTP Blueprint**: Enforces uniform compile-time API surfaces using the Curiously Recurring Template Pattern to eliminate `virtual` function calls during the transmission hot path.
- **Cache-Line Alignment**: Enforces `alignas(64)` on internal queue state to prevent False Sharing between the execution thread pushing data and the egress thread consuming it.

## 3. Pipeline Integration
Acts as the base template for any component sitting at the tail of the execution DAG. It guarantees that the handoff from the `branchless_engine` to the physical wire remains mathematically verifiable and latency-bound.