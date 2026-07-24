# ADR 008: Deterministic Error Handling

## Status
Accepted

## Context
Standard C++ exception handling (`try-catch`) introduces hidden control flow paths and non-deterministic overhead that is unacceptable in sub-microsecond pipelines. Unwinding the stack is a slow process that destroys instruction pipeline state.

## Decision
The use of C++ exceptions is strictly forbidden within the SLABFLUX core and hot-path logic. All errors must be handled via explicit return codes or `std::expected` (or custom result types) that are evaluated branchlessly.

## Consequences
- **Positives**: Guarantees deterministic execution time; no hidden stack-unwinding performance penalties.
- **Negatives**: Requires developers to explicitly propagate and handle error states, increasing code verbosity.
