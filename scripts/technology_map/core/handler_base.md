# SlabFlux Core: Handler Base (`handler_base.hpp`)

## 1. Architectural Overview
The `handler_base<T>` is the structural foundation (CRTP template) for all deterministic execution nodes plugged into the Cartesian `pipeline`. It enforces strict compilation invariants and standardizes the event-subscription interface.

## 2. Curiously Recurring Template Pattern (CRTP)
SlabFlux rejects `virtual` dispatch due to vtable lookup latencies. 
- `handler_base` leverages CRTP (`class MyHandler : public handler_base<MyHandler>`) to provide statically resolvable inherited behaviors.
- It injects metadata anchors (`parents`, `parents_fallback_anchor`) directly into the derived class, natively satisfying the `pipeline`'s Topological DAG extraction requirements without requiring external AST generation.

## 3. Interface Enforcement
Using C++20 `requires` clauses, the base class enforces that derived implementations conform to the strict API contract:
- They must implement `on(Event&, ...)` signatures.
- They must declare their temporal priority and phase affinity (e.g., `using priority = ...`).
If a developer builds a handler without specifying how it should be sequenced in the event DAG, the compilation fails immediately, safeguarding the timeline.

## 4. O(1) SFINAE Tracing
By inheriting from `handler_base`, the node natively integrates with `exact_ref` SFINAE probes. The dispatcher can seamlessly unroll the class hierarchy during Phase 3 (`unroll_hbases`), executing the perfect intersection of base-class methods and leaf-class overrides in place.