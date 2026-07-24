# SlabFlux Bridge: Bridge Sync (`bridge_sync.hpp`)

## 1. Architectural Overview
In the SlabFlux bimodal architecture, crossing the boundary from the high-velocity, deterministic hot-path (Trading/AI) into non-deterministic background domains (Logging, Telemetry, Auditing) is inherently dangerous. 
The `bridge_sync` provides the primary synchronization anchor, flawlessly moving memory ownership across thread contexts without invoking POSIX locks or stalling the hot path.

## 2. O(1) Boundary Handoff
Standard synchronization queues require deep copies or atomic contention to transfer data between threads. 
The `bridge_sync` eliminates this by utilizing physical memory sovereignty:
- Instead of copying payloads, the hot-path passes raw, pinned pointers directly into the synchronization boundary.
- The handoff executes in O(1) time using relaxed atomic sequence markers (Seqlock variants), allowing the primary execution core to instantly abandon the object and return to polling the network.

## 3. RAII Deletion Proxies
A critical challenge in zero-allocation, zero-copy systems is knowing when it is safe to recycle the memory back into the `spsc_pool` after the background thread has finished processing it.

The bridge solves this deterministically:
- It wraps the boundary pointer in a customized `consume()` payload proxy.
- When the cold-path thread (e.g., the telemetry aggregator) finishes reading the telemetry data and the proxy goes out of scope, its RAII destructor automatically invokes the origin pool's `release()` method.
- This guarantees zero memory leaks and mathematically sound cross-thread object lifecycles without distributed garbage collection.

## 4. Hardware SMT Scaling
To maximize bandwidth across NUMA nodes, `bridge_sync` perfectly aligns its internal state markers to 64-byte boundaries, preventing False Sharing between the hot-path publisher and the cold-path subscriber.