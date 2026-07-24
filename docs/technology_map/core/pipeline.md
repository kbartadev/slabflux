# SlabFlux Core: Cartesian Dispatch Pipeline (`pipeline.hpp`)
# Pipeline Integration Header (`pipeline.hpp`)

## 1. Architectural Overview
The `pipeline` is a 7D Cartesian Dispatcher that evaluates holonomy path ordering over discrete Event Directed Acyclic Graphs (DAGs). It acts as the backbone routing mechanism for incoming network ticks, dispatching them to domain handlers with zero virtual function overhead.

## 2. Compile-Time Topological Unrolling
Rather than maintaining dynamic subscriber lists (like standard pub/sub frameworks), the pipeline extracts handler dependencies via Typelist Algebra (`get_ancestors`, `topological_sort`).
- At compile time, the C++ compiler builds an ordered graph of how handlers should execute.
- It enforces "Leaf-First Ordering", ensuring base contexts are processed sequentially.
- The dispatch loop is completely flattened into consecutive inline assembly block invocations.

## 3. SFINAE Context Probing
Handlers do not need to implement rigid virtual interfaces. The `is_invocable_exact` templates dynamically probe if a handler implements `on(Event&, Context&)` versus `on(Event*)`.
- The pipeline dynamically injects `std::tuple` references or global thread-local contexts (`detail::get_global_context_instance`) depending on the exact footprint requested by the handler.

## 4. Hardware Halt Interception
The pipeline actively monitors execution outcomes via `is_halted(e)`. 
If a specific `scoped_ptr` is released by an upstream handler, the loop detects the boolean halt state at compile-time and deterministically short-circuits the remaining pipeline execution, saving instruction bandwidth.