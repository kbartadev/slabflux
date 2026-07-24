# Blueprint: pipeline.hpp

## Architectural Overview
The `pipeline` is a generalized, static unroll-based dispatch mechanism. It evaluates the set of provided handlers and target event types directly against the available overload patterns (`on(...)` signatures). Its operation is declarative and strictly type-driven. It does not construct an explicit inheritance graph; instead, the execution paths and call sequences are determined dynamically at compile-time purely by the available `on(...)` signatures, rather than traversing a full type hierarchy.

## Core Logic & Mechanisms
- **Signature-Driven Routing**: Relies entirely on C++20 `requires` clauses and overload resolution to determine if a handler can accept a given event or context, eliminating the need for heavy topological sorting of types.
- **Static Unrolling**: Utilizes C++17 fold expressions to flatten the handler iteration. The compiler inherently strips away `false` branches where signatures do not match, producing a contiguous block of inline execution.
- **Declarative Context Resolution**: Heterogeneous states (Contexts) are resolved at dispatch time based on the `event_context_map` and the specific parameters requested by each handler's `on(...)` signature.
- **Decoupled Execution Paths**: By not building an explicit inheritance or execution graph, the engine remains highly flexible. A handler is invoked if and only if it declares a valid matching signature for the dispatched event and available context layers.