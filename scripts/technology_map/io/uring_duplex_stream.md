# Blueprint: uring_duplex_stream.hpp

## Architectural Overview
Fuses `uring_ingress_stream` and `uring_egress_stream` into a single interleaved execution matrix running over a unified `io_uring` instance to maximize CPU cache residency on high-frequency socket threads.

## Core Logic & Mechanisms
- **Unified Polling Loop**: The `poll_runtime` function sequences both CQE checks for network receipts and transmission acknowledgments within the same CPU clock loop, preventing context switching across dual rings.
- **Memory Locality**: Isolates ingress pool and egress logic, yet evaluates them strictly consecutively to preserve L1 cache heat.
- **Deadlock Avoidance**: Enforces that buffer exhaustion on the transmission side never prevents the ingestion logic from pulling completion entries off the CQE ring.