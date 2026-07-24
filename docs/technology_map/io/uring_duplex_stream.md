# SlabFlux I/O: uring_duplex_stream (`slabflux/io/uring_duplex_stream.hpp`)

## 1. Architectural Justification
The `uring_duplex_stream` component governs bidirectional, connection-oriented (TCP) workloads over the `io_uring` subsystem. Unlike datagram engines where message boundaries are hardware-aligned, stream engines must continuously track and reconstruct fragmented state machines without blocking the asynchronous ring.

This module acts as the unified gateway for streaming protocols, guaranteeing that long-lived connections (e.g., FIX, WebSocket, or custom TCP trading feeds) do not exhaust thread resources or cause Head-of-Line (HoL) blocking across the compute matrix.

## 2. Hardware Implementation Directives
- **Continuous Stream Polling**: Dispatches chained `IORING_OP_RECV` and `IORING_OP_SEND` operations. To minimize Submission Queue (SQ) updates, it leverages `IOSQE_IO_LINK` where appropriate to batch sequential stream operations on the same file descriptor.
- **Symmetric Matrix Handoff**: 
  - **Ingress**: Drains raw stream bytes into a continuous, mapped user-space buffer.
  - **Egress**: Consumes disjoint state pointers from the execution manifold and maps them to contiguous outbound TCP segments using scatter-gather `io_uring` writes.
- **Socket Lifecycle Tying**: Gracefully handles asynchronous `EOF` (0-byte reads) and `RST` signals, cleanly unlinking the socket descriptor from the polling matrix and recycling the session memory back to the global pool without invoking kernel-space `close()` blocking.

## 3. Algorithmic Backpressure
Because TCP streams are subject to dynamic sliding windows and network congestion, the `uring_duplex_stream` acts as a shock absorber. If the kernel's egress TCP buffers fill (indicated by short-writes in the CQE), the duplex engine seamlessly pauses the specific session's egress conduit while allowing the rest of the execution manifold and other sockets to proceed at line-rate.