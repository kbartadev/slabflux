# SlabFlux Core: Round-Robin Poller (`round_robin_poller.hpp`)

## 1. Architectural Overview
The `round_robin_poller` is a high-performance Fan-In (multiplexing) node. It is designed to deterministically aggregate data from multiple asynchronous upstream sources (e.g., redundant market data feeds, separate NIC queues) into a single, linearized execution stream.

## 2. Wait-Free Ring Traversal
Instead of utilizing blocking `epoll` or OS-level `select` mechanisms, the poller maintains an array of pointers to localized `spsc_ring_conduit` instances.
- The polling thread spins in an infinite, tight `while(true)` loop (the `stall_free_nexus`), continuously evaluating the egress gates of the attached conduits.
- Because the internal conduits are wait-free and physically isolated by cache-lines, checking 16 separate queues takes fewer than 30 CPU cycles total.

## 3. Fair-Share Extraction (Starvation Immunity)
In high-throughput environments, a single heavily congested feed (e.g., an options order book) could monopolize the execution thread, starving out sparse but critical feeds (e.g., emergency kill-switch commands).

The `round_robin_poller` employs strict quota-based extraction:
- During each pass over a specific conduit, it extracts a maximum of `N` events (e.g., a batch of 8) before forcing the iterator to advance to the next connected conduit.
- This mathematically guarantees that no single upstream producer can saturate the logic core, ensuring absolute fairness and predictable worst-case latency for all connected topologies.

## 4. SMT / Hyper-Threading Yielding
When all connected conduits are physically empty, aggressive polling can monopolize the physical ALU, choking out hyper-threading siblings or overheating the core.
The poller detects idle loops and executes the `_mm_pause()` (or `TPAUSE`) intrinsic. This signals the hardware dispatcher to temporarily suspend instruction decoding for a fraction of a microsecond, vastly reducing thermal load without incurring deep C-state wake latencies.