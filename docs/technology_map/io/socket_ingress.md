# SlabFlux I/O: socket_ingress (`slabflux/io/socket_ingress.hpp`)

## 1. Architectural Justification
While SlabFlux prioritizes strictly kernel-bypass mechanisms (`AF_XDP`, `io_uring`), `socket_ingress` serves as the robust fallback layer for standard POSIX networking environments. It maps synchronous, blocking network infrastructure into the asynchronous `spsc_conduit` event streams required by the engine.

## 2. Hardware Implementation Directives
- **Epoll Event Multiplexing**: Implements a dedicated spin-poller executing `epoll_wait` configured with `EPOLLIN | EPOLLET` (Edge-Triggered) flags, extracting maximum event-rate throughput from standard kernel network stacks.
- **Non-Blocking Drains**: Upon notification, the loop continuously invokes `recv()` with the `MSG_DONTWAIT` flag until an `EAGAIN` condition is encountered. This ensures the kernel socket buffer is completely drained into the user-space matrix in a single traversal burst.
- **Thread Isolation**: The socket receiver runs on a pinned hardware thread physically decoupled from the deterministic business logic. This barrier prevents OS-level kernel jitter and interrupt handlers from polluting the L1 cache of the algorithmic core.

## 3. Pointer Translation
Packets retrieved from the kernel are written directly into pre-allocated `managed_data` blocks sourced from the global memory arena. The boundary adapter immediately strips the POSIX metadata and pushes the validated struct pointers into the mesh for deterministic execution.