# Blueprint: mpsc_pool.hpp

## Architectural Overview
A Multi-Producer, Single-Consumer bounded memory arena. Designed specifically for asymmetrical Fan-In topologies where multiple network threads request allocations that are reclaimed by a single central execution engine.

## Core Logic & Mechanisms
- **Asymmetric Atomics**: Optimizes the consumer reclamation path (`reclaim_returns`) by isolating the heavier atomic multi-producer claims from the single-threaded release mechanism.
- **Automatic Reclamation Strategy**: Incorporates `reclaim_strategy::automatic` to seamlessly sweep and reset finalized elements when the pool senses capacity exhaustion, preserving deterministic runtime without manual garbage-collection threads.