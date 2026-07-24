# Foundation: SPSC Conduit (`slabflux/core/spsc_conduit.hpp`)

## 1. Architectural Justification
The `spsc_conduit` maps directly to the theoretical limits of the MESI cache coherence protocol. By ensuring that `head` and `tail` cursors reside on separate physical cache lines, the structure completely bypasses Read-For-Ownership (RFO) interconnect stalls on the QPI/Infinity Fabric during parallel processing.

## 2. Hardware Implementation Directives
- **L1 Cache Sovereign Bounding**: `alignas(64)` padding forces cache-line perfection.
- **Memory Ordering**: Utilizes `std::memory_order_acquire` and `std::memory_order_release` to establish strict *happens-before* edges without locking the CPU memory bus via expensive `MFENCE` instructions.
- **Index Masking**: Wraps ring cursors using `cursor & (Capacity - 1)` to eliminate scalar division (`DIV`) latencies.

## 3. Bibliography & Proofs
1. **Intel Corporation**. (2023). *Intel 64 and IA-32 Architectures Software Developer’s Manual, Volume 3A: System Programming Guide*. Chapter 11: Memory Cache Control (MESI protocol mechanics).
2. **Herlihy, M.** (1991). *Wait-free synchronization*. ACM Transactions on Programming Languages and Systems (TOPLAS), 13(1), 124-149. (Mathematical proof for lock-free progression boundaries).
3. **McKenney, P. E.** (2017). *Is Parallel Programming Hard, And, If So, What Can You Do About It?*. kernel.org. Chapter 5: Counting (Cache Coherence properties and False Sharing).
4. **Lamport, L.** (1977). *Proving the Correctness of Multiprocess Programs*. IEEE Transactions on Software Engineering.