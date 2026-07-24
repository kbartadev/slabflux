# Blueprint: spsc_conduit.hpp

## Architectural Overview
Highly optimized single-producer single-consumer lock-free ring. Operates strictly on raw pointer transmission with specific cache alignment to completely prevent false sharing.

## Core Logic & Mechanisms
- **Relaxed Memory Semantics**: Updates to physical index positions are driven via `std::memory_order_relaxed` internally, utilizing `acquire` and `release` strictly at the structural boundary, avoiding global sequential consistency fences.
- **Backpressure Return Bounds**: The `try_push()` operation rejects pointer ingestion directly in O(1) latency when capacity matches the opposing read counter, triggering explicit pipeline backpressure.