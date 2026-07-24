# Foundation: Deterministic State Machine (`slabflux/workflow/state_machine.hpp`)

## 1. Architectural Justification
Managing complex multi-stage order lifecycles (e.g., Pending -> Acked -> Filled) typically requires heap-allocated objects and `switch`/`case` trees. The `state_machine` module enforces a zero-allocation, transition-table-based deterministic finite automaton (DFA).

## 2. Hardware Implementation Directives
- **Flat Matrix Layout**: Workflows are tracked in a statically allocated `std::array`. Looking up a workflow's state is an `O(1)` array indexing operation `workflows_[id & MASK]`.
- **2D Transition Jump Tables**: Transitions are defined at compile time. The engine uses the `(CurrentState, EventType)` integer tuple to index a 2D matrix of function pointers, entirely avoiding branch prediction penalties during state mutations.
- **Inline Causal Updates**: Successfully executed transitions return a bitmask that instantly triggers the downstream `engine_pulse` to embed the transition into the persistent `durable_journal`.

## 3. Bibliography & Proofs
1. **Harel, D.** (1987). *Statecharts: A visual formalism for complex systems*. Science of Computer Programming.
2. **Vandevoorde, D., & Josuttis, N. M.** (2002). *C++ Templates*. (Compile-time transition tables).
3. **Fog, Agner**. (2021). *Optimizing subroutines in assembly language*. (Jump tables and instruction cache footprint).