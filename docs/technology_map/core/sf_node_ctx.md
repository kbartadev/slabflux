# SlabFlux Core: Sovereign Node Context (`sf_node_ctx.hpp`)

## 1. Architectural Overview
To maintain a strict, replayable timeline across a distributed system, every thread and logical core must be perfectly aware of its chronological position. The `sf_node_ctx` (Sovereign Node Context) acts as the localized temporal anchor for individual execution pipelines.

## 2. Logical Sequence Tracking
The context houses the definitive LSN (Logical Sequence Number) and watermarks for the active thread:
- **Current LSN**: The absolute sequence identifier of the event currently being processed.
- **Commit Horizon**: The highest LSN that has been mathematically verified and successfully flushed to the `durable_journal` or broadcasted to the network.

By embedding this tracking data locally within the pipeline's execution loop (via `context_vault`), the `branchless_engine` can update its chronological state without invoking global mutexes or atomic cross-core cache invalidations.

## 3. Gap Analysis & Replay
When a node initializes or recovers from a failover:
- The `sf_node_ctx` is queried to establish the last known-good LSN marker.
- The `replay_manager` uses this exact marker to determine where to begin injecting historical `durable_journal` events, guaranteeing that the deterministic state machine resumes precisely where it failed without duplicating side-effects.

## 4. Thread Pinning Context
Alongside sequence tracking, the `sf_node_ctx` securely caches the hardware topology bindings for the thread (e.g., NUMA node ID, physical core ID). This allows subroutines (like the `numa_allocator`) to execute localized memory operations efficiently without redundantly querying the OS kernel.