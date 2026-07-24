# Blueprint: LSN Engine

## Architectural Overview
The Log Sequence Number (LSN) Engine establishes absolute temporal and causal boundaries across independent lock-free architectures without relying on global hardware mutexes.

## Core Logic & Mechanisms
- **Monotonic Sequence Generation**: Generates strictly increasing sequence identifiers mapped to specific memory allocations to define a strict order of operations for asynchronous journal writers.
- **Causality Vectors**: Embeds the active LSN into outgoing `spsc_conduit` frames, ensuring downstream components can reconstruct the causal chain and detect sequence gaps or jitter.
- **Barrier Synchronization**: Enforces execution fences by tracking the global "safe" LSN high-watermark, prohibiting side-effects until the state is fully resolved by the underlying mesh.