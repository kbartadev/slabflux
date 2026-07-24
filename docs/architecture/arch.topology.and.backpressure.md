# Topology and Backpressure Mechanics

SLABFLUX treats data flow as a physical fluid. If a downstream pipe is blocked, the system must react deterministically.

## Strict SPSC Topology

Every conduit is strictly Single-Producer Single-Consumer. This guarantees that only two cores are ever accessing the atomic indices, eliminating L1 cache bouncing.

## Deterministic Drops (Backpressure)

When a producer attempts to `push()` into a full conduit, the push operation returns `false`.
- If routed directly, the producer retains ownership of the `event_ptr`.
- If routed via a switch, the switch will detect the failure. Instead of blocking or spinning indefinitely, it drops the event. The `event_ptr` goes out of scope and the memory is instantly reclaimed by the pool.

This ensures the system never deadlocks under extreme network saturation.
