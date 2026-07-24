# Blueprint: dispatcher.hpp

## Architectural Overview
Serves as the structural baseline and legacy metaprogramming engine, providing fundamental typelist algebra for event routing before the deep Cartesian pipeline optimizations take over.

## Core Logic & Mechanisms
- **Typelist Algebra**: Executes raw variadic template evaluations (`typelist_append`, `typelist_cat`, `sort_typelist`) to establish deterministic handler execution hierarchies.
- **Graph Resolution**: Evaluates parent-child relationships structurally via SFINAE, injecting them into a resolved static execution matrix for older fallback pipeline definitions.