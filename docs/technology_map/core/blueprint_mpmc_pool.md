# Blueprint: mpmc_pool.hpp

## Architectural Overview
Provides a heavily contended, fixed-size memory arena safe for simultaneous parallel allocations across multiple threads, replacing the global heap allocator (`malloc`/`new`).

## Core Logic & Mechanisms
- **Sequence Locks**: Utilizes independent sequence arrays mapped 1:1 with data slots to ensure thread-safe slot claiming and releasing without executing global mutex locks.
- **Cache-Aligned Cell Padding**: Prevents `std::atomic` sequence counters from sharing the same hardware cache line as the payload structure, eliminating read-for-ownership (RFO) ping-pong stalls during multi-core contention.
- **Perfect Forwarding**: The `make(...)` API perfectly forwards arguments via placement-new directly into the locked buffer array.