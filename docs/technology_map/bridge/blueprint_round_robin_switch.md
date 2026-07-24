# Blueprint: round_robin_switch.hpp

## Architectural Overview
Provides lock-free Fan-Out routing. Acts as a core sharding mechanism that balances massive incoming traffic loads evenly across multiple independent execution domains.

## Core Logic & Mechanisms
- **Branchless Target Resolution**: Replaces `if-else` routing heuristics with pure mathematical modulo division against registered worker pipelines.
- **Hot-Path Yielding**: Detects target-conduit saturation. If a shard falls behind, the router skips it without blocking the primary ingestion loop.