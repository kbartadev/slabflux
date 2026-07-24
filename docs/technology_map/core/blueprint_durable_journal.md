# Blueprint: durable_journal.hpp

## Architectural Overview
The primary persistence layer for the Causal Mesh. It asynchronously flushes Lamport-sequenced network frames to NVMe storage via `io_uring` to provide the exact baseline required for the Replay Saga engine.