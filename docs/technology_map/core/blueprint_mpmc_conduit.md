# Blueprint: mpmc_conduit.hpp

## Architectural Overview
The primary lock-free bus infrastructure for N-to-N thread communication, allowing highly parallelized worker pools to safely push and pop raw pointers concurrently.

## Core Logic & Mechanisms
- **Ticket Fetch-Add Matrix**: Uses `std::atomic::fetch_add` with `memory_order_relaxed` to rapidly reserve logical ring bounds before committing the physical pointer copy, minimizing critical-section latency.
- **Yield Backoff**: When sequential bounds mismatch (indicating a thread is currently in the middle of a write block), the caller invokes hardware pause intrinsics (`_mm_pause`) rather than spin-locking natively.