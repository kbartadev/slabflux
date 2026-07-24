# SlabFlux I/O: io_uring Durable Journal (`slabflux/io/io_uring_durable_journal.hpp`)

## 1. Architectural Justification
Implements the critical event-sourcing persistence layer. Deterministic systems require every state mutation to be logged durably before external acknowledgment, yet blocking on disk I/O destroys latency.

## 2. Hardware Implementation Directives
- **O_DIRECT Bypass**: Opens the journal files with `O_DIRECT`, bypassing the Linux page cache. NVMe DMA engines read directly from the application's ring buffers.
- **Asynchronous Commits**: Uses `io_uring` to queue write operations wait-free. The global Logical Sequence Number (LSN) is only advanced when the Completion Queue Entry (CQE) is processed asynchronously.
- **Zero Dynamic Allocation**: State envelopes pushed into the journal buffer are recycled purely by advancing head/tail bounds.

## 3. Pipeline Integration
Acts as the critical persistence listener on the outbound state conduit. Before any deterministic execution output is allowed to be transmitted to external network peers via `uring_egress` or `socket_egress`, the journal must asynchronously signal that the associated LSN has been physically committed to NVMe storage, enforcing strict event-sourcing guarantees.