# SlabFlux I/O: io_uring Egress (`slabflux/io/uring_egress.hpp`)

## 1. Architectural Justification
The `uring_egress` component manages the high-throughput transmission of UDP datagrams without blocking the main execution thread. By utilizing the `io_uring` submission queue (SQ), it eliminates synchronous `sendto()` system calls entirely.

## 2. Hardware Implementation Directives
- **Batched Submissions**: Outbound payloads are formatted into `io_uring_sqe` (Submission Queue Entries) directly within the shared memory ring. The engine submits batches of network frames in a single syscall (or zero syscalls if `SQPOLL` is active).
- **Zero-Copy Architecture**: Payloads are written into pre-registered DMA buffers. The NIC reads directly from these buffers via kernel bypass, circumventing `sk_buff` allocations.

## 3. Pipeline Integration
Consumes pointers directly from the outbound `spsc_conduit`, preventing any queueing latency inside the deterministic boundary from affecting wire-time logic.