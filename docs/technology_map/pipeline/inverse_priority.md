# SlabFlux Pipeline: Inverse Priority (`slabflux/pipeline/inverse_priority.hpp`)

## 1. Architectural Justification
In a deterministic DAG, the execution order of multiple handlers is paramount. `inverse_priority` mathematically proves and statically enforces the exact topological execution sequence, ensuring sibling conflicts are caught at compile-time.

## 2. Compile-Time Implementation Directives
- **Static Bubble Sort**: Implements a compile-time sorting algorithm over `typelist` structures. Uses descendant counting and priority tokens (`std::size_t`) to rank execution precedence.
- **Sibling Conflict Detection**: Rejects duplicate sibling priorities via `static_assert`, preventing non-deterministic execution paths from compiling.

## 3. Pipeline Integration
Used by `ancestor_expansion` to finalize the topological array. The resulting strictly-ordered typelist is fed directly to the `dispatch_unroller`, guaranteeing reproducible chronological state mutations.