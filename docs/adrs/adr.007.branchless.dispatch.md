# ADR 007: Branchless Dispatch Invariant

## Status
Accepted

## Context
Conditional branches (`if/else`, `switch`) in the hot-path introduce pipeline stalls due to branch misprediction. Deterministic performance requires the execution flow to be as predictable as possible for the CPU's branch predictor.

## Decision
All runtime routing (dispatch) logic must be implemented using branchless techniques (e.g., C++17 fold expressions, jump tables, or bitwise-based indexing). Ternary operators are only permitted if they can be optimized to `CMOV` (conditional move) instructions.

## Consequences
- **Positives**: Guaranteed O(1) execution time regardless of the complexity of the event type set. Eliminates high-latency branch misprediction penalties.
- **Negatives**: Increases code complexity; requires developers to have a deep understanding of template meta-programming and compiler code generation.
