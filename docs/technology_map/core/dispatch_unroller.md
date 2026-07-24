# SlabFlux Pipeline: Dispatch Unroller (`slabflux/pipeline/dispatch_unroller.hpp`)

## 1. Architectural Justification
Runtime polymorphism (`virtual` functions and `dynamic_cast`) destroys the instruction cache and branch predictor. The `dispatch_unroller` forces the compiler to expand the entire execution topology into a flat, inline instruction sequence, achieving absolute O(1) latency bounds.

## 2. Compile-Time Implementation Directives
- **Fold Expressions**: Leverages generic lambdas and C++ fold expressions `( ..., func() )` over `std::index_sequence`. 
- **Forced Inlining**: Forces the compiler to inline all handler calls, allowing the optimizer to interleave instruction pipelines across multiple handlers without `call` or `ret` overhead.

## 3. Pipeline Integration
It is the execution heart of the `pipeline::dispatch()` method. It iterates over the topologically sorted handler typelist and invokes `try_one_event`, immediately halting execution if a handler consumes the event and requests a halt.