# SlabFlux Pipeline: Typelist Algebra (`slabflux/pipeline/typelist_algebra.hpp`)

## 1. Architectural Justification
The deterministic dispatcher needs to resolve complex Event and Handler topologies (Directed Acyclic Graphs) entirely at compile time to achieve zero-overhead routing. `typelist_algebra` provides the foundational mathematical operations (concatenation, unique filtering, mapping) over variadic parameter packs to make this possible.

## 2. Compile-Time Implementation Directives
- **Zero Runtime Footprint**: The operations exist purely as C++ template instantiations. They evaluate entirely within the compiler's frontend and yield zero assembly instructions.
- **Recursive Resolution**: Utilizes deep template recursion (`std::conditional_t`, `contains_v`) to manipulate type structures before the binary is even assembled, bypassing runtime dynamic allocations.

## 3. Pipeline Integration
Acts as the absolute base dependency for all other pipeline modules (such as `ancestor_expansion` and `dispatch_unroller`). It enables the mathematical resolution of event inheritance chains, forming the bedrock of the branchless dispatcher.