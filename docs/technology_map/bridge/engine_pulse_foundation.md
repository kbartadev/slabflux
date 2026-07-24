# Foundation: Engine Pulse (`slabflux/bridge/engine_pulse.hpp`)

## 1. Architectural Justification
The `engine_pulse` subsystem acts as the high-throughput sequencing and transactional barrier mechanism within the SlabFlux RTE. It guarantees strict monotonic event ordering and gap-less sequence propagation across isolated lock-free rings without invoking dynamic memory allocations.

## 2. Hardware Implementation Directives
- **Pulse Shared State**: Encapsulates the global `last_lsn` into a strictly cache-aligned structure. This isolates intensely updated atomic counters from payload elements, neutralizing cross-thread cache invalidation (False Sharing).
- **Monotonic Reservations**: Grants logic cores the ability to deterministically advance the sequence clock using relaxed atomic operations (`memory_order_relaxed`) before executing business logic.
- **Hardware Execution Fences**: Injects an explicit `_mm_sfence` (Store Fence) before pushing updated sequential markers downstream, ensuring all passive observers see the finalized state completely intact.

## 3. Bibliography & Proofs
1. **Fowler, M.** (2011). *The LMAX Architecture*. (Mechanical sympathy and sequence-based transactional barriers).
2. **McKenney, P. E.** (2017). *Is Parallel Programming Hard, And, If So, What Can You Do About It?* (Memory ordering and SFENCE utilization).
3. **Herlihy, M., & Shavit, N.** (2008). *The Art of Multiprocessor Programming*. (Sequence reservations).