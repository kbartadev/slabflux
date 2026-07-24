# Foundation: Event Arbiter (`slabflux/rte/event_arbiter.hpp`)

## 1. Architectural Justification
Standard message brokers use heap-allocated priority queues (`std::priority_queue`) that invoke `O(log N)` rebalancing. In a sub-microsecond framework, the `event_arbiter` replaces sorting with a statically synthesized, branchless polling trace to evaluate conduits in strictly `O(1)` time.

## 2. Hardware Implementation Directives
- **Static Hierarchical Routing**: Inlines the pop operations of the Admin, Time, and Data conduits in a strict sequential trace. This allows the CPU instruction prefetcher to perfectly anticipate the control flow.
- **Starvation Prevention (5-Strike Rule)**: Maintains a local CPU register counter for empty Admin polls. If the Admin conduit is empty 5 times, data is evaluated. This guarantees that control-plane commands (like configuration reloads) are never starved by high-frequency network data floods.

## 3. Bibliography & Proofs
1. **LMAX Exchange**. (2011). *The LMAX Architecture*. (Deterministic event scheduling and multi-channel input handling).
2. **Intel Corporation**. *Intel 64 and IA-32 Architectures Optimization Reference Manual*. (Branch prediction limits and flat control flow).
3. **Bakhvalov, A.** (2022). *Performance Analysis and Tuning on Modern CPUs*.