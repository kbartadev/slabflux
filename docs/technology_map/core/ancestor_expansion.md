# SlabFlux Pipeline: Ancestor Expansion (`slabflux/pipeline/ancestor_expansion.hpp`)

## 1. Architectural Justification
Business logic often relies on hierarchical event structures (e.g., `MarketData` -> `Trade` -> `EquityTrade`). `ancestor_expansion` crawls these definitions and mathematically flattens them into a Directed Acyclic Graph (DAG), ensuring handlers process base events before their descendants.

## 2. Compile-Time Implementation Directives
- **Transitive Closure**: Uses recursive type expansion to calculate the complete inheritance tree of the `parents` typelist without utilizing runtime RTTI or `dynamic_cast`.
- **Descendant Weighing**: Calculates descendant counts for every node. This creates deterministic topological weights used for sorting execution precedence.

## 3. Pipeline Integration
Executes during the instantiation of `pipeline::dispatch()`. It resolves the incoming physical event type into its full inheritance DAG, which is then fed into the `unroll_pipeline` template to trigger the exact sequence of typed user handlers.