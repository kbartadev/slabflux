# SlabFlux Net: Server Ingress (`server_ingress.hpp`)

## 1. Architectural Overview
The `server_ingress` component acts as the ultra-low latency gateway between the Linux kernel's network stack and the deterministic Shared Memory (SHM) frame buffers. It strictly utilizes modern `io_uring` mechanics.

## 2. Zero-Syscall Polling
The ingress queue is instantiated with `IORING_SETUP_SQPOLL` and `IORING_SETUP_SQ_AFF`.
- The Linux kernel spins a dedicated kernel thread (`io_wqe`) pinned to an isolated CPU core.
- When the trading logic wants to read from the socket or flush the buffer ring, it simply writes to the shared memory Ring-Buffer. No `recvmsg` or `epoll_wait` system calls are ever executed, eliminating context-switch overhead.

## 3. Cooperative Task Routing
To further drive down latency, the ring uses `IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN`. This prevents the kernel from unnecessarily interrupting the application thread with signals, deferring task work to explicit application-defined polling boundaries.

## 4. Authoritative Sequence Binding
Upon reaping a Completion Queue Entry (CQE), the ingress instantly fetches the next atomic LSN from the `sequence_generator` and tags the frame alongside a physical `__rdtsc()` ingress timestamp. This guarantees a monolithic, causal timeline for every byte entering the trading mesh.