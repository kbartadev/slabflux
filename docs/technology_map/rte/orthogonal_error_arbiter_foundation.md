# Foundation: Orthogonal Error Arbiter (`slabflux/rte/orthogonal_error_arbiter.hpp`)

## 1. Architectural Justification
Classical error handling (Exceptions, `errno`, `std::expected`) forces branching control flow, destroying Instruction Level Parallelism (ILP). Queuing errors causes unbounded memory growth and backpressure. The `orthogonal_error_arbiter` models errors as physical tensors in a predefined field, executing in strictly O(1) time without branches, queues, or memory allocation.

## 2. Hardware Implementation Directives
- **Gödel Numbering (Prime Factorization)**: Error hierarchies are encoded as products of prime numbers. Checking if a specific error belongs to a category is a single, branchless modulo operation (`error % category == 0`), replacing expensive inheritance trees.
- **Wait-Free Subsumption**: The field is an array of `std::atomic<uint64_t>`. A new error uses a Knuth Multiplicative Hash to find its coordinate and simply overwrites (`store`) any existing error in that slot. This guarantees absolute immunity to Out-Of-Memory (OOM) and backpressure during catastrophic failure storms.
- **Destructive Reading**: The consumer extracts telemetry using a single `exchange(0)` instruction, pulling the data and clearing the slot simultaneously without CAS loops.

## 3. Bibliography & Proofs
1. **Gödel, K.** (1931). *Über formal unentscheidbare Sätze der Principia Mathematica und verwandter Systeme I*. (Prime factorization as a mathematical basis for structural topological encoding).
2. **Knuth, D. E.** (1998). *The Art of Computer Programming, Volume 3: Sorting and Searching*. (Multiplicative hashing for uniform spatial distribution).
3. **Dijkstra, E. W.** (1974). *Self-stabilizing systems in spite of distributed control*. CACM. (Subsumption and state-overwriting for autonomic system stabilization).