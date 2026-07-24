# SlabFlux Net: State Streamer (`slabflux/net/state_streamer.hpp`)

## 1. Architectural Justification
To maintain event-sourcing determinism, application state must be durably recorded. However, invoking standard `write()` or `fsync()` syscalls halts the execution manifold. The `state_streamer` acts as an asynchronous memory archival component, offloading deep-buffer NVMe disk writes from the primary algorithm loop.

## 2. Hardware Implementation Directives
- **io_uring Polling**: Commits disk writes via the `io_uring` Submission Queue. The kernel processes `O_DIRECT` block I/O asynchronously, allowing the algorithm to continue executing at L1 cache speeds.
- **DMA Memory Pinning**: Memory regions bound for the NVMe drive are pre-registered and pinned. The PCIe controller executes DMA reads directly from the user-space buffer, preventing CPU-bound copy operations into the OS page cache.

## 3. Lifecycle Integration
The streamer consumes state snapshots emitted from the wait-free conduits. It sequences the payloads sequentially, flushing them to non-volatile storage while asynchronously tracking completion queues to advance the global Logical Sequence Number (LSN) watermark.