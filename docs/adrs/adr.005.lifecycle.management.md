# ADR 005: Pointer-Centric Lifecycle Management

## Status
Accepted

## Context
High-level smart pointers (e.g., `std::shared_ptr`) introduce atomic reference counting overhead, which induces heavy cache-coherency traffic and branch mispredictions. Deterministic systems require absolute ownership control.

## Decision
The usage of `std::shared_ptr` or `std::unique_ptr` is strictly forbidden within the hot-path. All event and resource lifecycles must be managed via raw pointers (`T*`), with manual `pool.release()` calls at the terminal end of the execution pipeline.

## Consequences
- **Positives**: Removes atomic operations from the hot-path, achieving true O(1) execution time.
- **Negatives**: Shifts the responsibility of memory safety and leak prevention entirely to the application developer. Usage of `slabflux::core::pool` is mandatory.
