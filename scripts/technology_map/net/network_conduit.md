# SlabFlux Net: Network Conduit (`network_conduit.hpp`)

## 1. Architectural Overview
The `network_conduit` provides an ultra-low latency, lock-free transmission bus that completely decouples deterministic business logic from the unpredictable stalls of OS-level network sockets. It provides fallback support for POSIX sockets while optimizing heavily for zero-syscall `io_uring` backends.

## 2. Wire Protocol Decomposer
To transfer typed C++ events across TCP/UDP without the latency of JSON or Protobuf:
- The conduit utilizes `wire_protocol<T>` to seamlessly translate strongly-typed domain events into cache-aligned byte frames (`wire_frame<T>`).
- This uses bit-perfect `reinterpret_cast` memory transfers, strictly avoiding dynamic serialization loops, heap allocations, or memory fragmentation.

## 3. Micro-Architectural Interleaving

### Shadow Caching
The conduit implements localized shadow registers (`cached_tx_tail_`) for ring buffer indices. This prevents the producer (Logic Core) and consumer (I/O Flush Core) from constantly polling cross-core atomic variables, virtually eliminating L3 cache bouncing.

### Software Pipelining
It actively issues non-blocking hardware prefetch instructions (`_mm_prefetch(..., _MM_HINT_T0)`) for the *next* sequential memory slot in the queue before serializing the current one. This elegantly overlaps DRAM memory access latency with active CPU instruction execution.

## 4. io_uring Acceleration
In Linux 5.1+ environments, the `network_conduit_uring` implementation leverages the `io_uring` API for absolute kernel-bypass transmission.
- It binds directly to fixed kernel file descriptors.
- It submits `IORING_OP_SEND` SQEs (Submission Queue Entries) to a dedicated SQPOLL thread.
- The transmission to the NIC occurs entirely via shared memory coordination, eliminating `send()` system calls and context switches completely.