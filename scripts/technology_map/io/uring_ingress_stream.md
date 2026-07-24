# Blueprint: uring_ingress_stream.hpp

## Architectural Overview
Provides discrete, zero-syscall network ingestion using kernel-bypass `io_uring` polling mechanics mapped directly into application pipeline endpoints.

## Core Logic & Mechanisms
- **SQPOLL Sweeping**: Establishes a dedicated kernel thread utilizing `IORING_SETUP_SQPOLL` to harvest packets from socket buffers without incurring POSIX context-switch latencies.
- **Provided Buffer Ring Integration**: Employs `io_uring_buf_ring` to project the memory addresses of pre-allocated pool blocks directly into the kernel, allowing raw DMA-like ingestion.
- **Hot-Path Polling & Prefetching**: `poll_hot_path` reads Completion Queue Entries (CQEs) and invokes `pipeline.process()` inline. Simultaneously triggers `_mm_prefetch` for the next predicted payload block to neutralize RAM access stalls.