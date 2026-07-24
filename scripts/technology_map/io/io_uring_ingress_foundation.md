# Foundation: io_uring Ingress (`slabflux/io/io_uring_ingress.hpp`)

## 1. Architectural Justification
Standard POSIX network reception relies on synchronous `recv()` calls or event-driven `epoll_wait()` loops, both of which require traversing the user/kernel boundary, incurring expensive context switch penalties. The `io_uring_ingress` provides an extraordinarily high-performance alternative, leveraging shared memory queues to achieve near-kernel-bypass latencies on generic sockets.

## 2. Hardware Implementation Directives
- **SQPOLL Kernel Threading**: Initialized with `IORING_SETUP_SQPOLL`. The Linux kernel dedicates an internal thread to actively poll the Submission Queue (SQ) residing in shared memory, neutralizing system calls entirely on the hot path.
- **Zero-Copy Registered Buffers**: Pre-allocates a massive array of aligned `spsc_pool` blocks and registers them permanently via `IORING_REGISTER_BUFFERS`. The kernel DMA engine deposits payloads directly into these pinned user-space buffers, bypassing the `sk_buff` payload replication.
- **Multishot Ring Handoff**: Extracts Completion Queue Entries (CQEs) and instantly wraps the payload pointers into `managed_data` structures, passing them wait-free into the `spsc_conduit` without intermediate allocations.

## 3. Bibliography & Proofs
1. **Axboe, J.** (2019). *Efficient IO with io_uring*. Linux Kernel Engineering Documentation. (Shared memory ring geometry and SQPOLL isolation).
2. **Corbet, J.** (2019). *Ringing in a new asynchronous I/O API*. LWN.net.
3. **Silberschatz, A., Galvin, P. B., & Gagne, G.** (2018). *Operating System Concepts*. (Context switch latency mathematics).