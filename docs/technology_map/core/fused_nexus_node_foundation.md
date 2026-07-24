# Foundation: Fused Nexus Node (`slabflux/core/fused_nexus_node.hpp`)

## 1. Architectural Justification
The `fused_nexus_node` acts as the ultra-low latency gateway between the Linux kernel's network stack and the deterministic Shared Memory (SHM) frame buffers. By fusing together Multishot Buffer Rings and SQPOLL threads, it completely eradicates context-switch overhead during network ingestion.

## 2. Hardware Implementation Directives
- **Zero-Syscall Polling**: Configures `io_uring` with `IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF`. The Linux kernel spins a dedicated kernel thread pinned to an isolated CPU core, polling the ring buffer entirely via shared memory.
- **Multishot Buffer Rings (`io_uring_buf_ring`)**: The DMA memory pool (`pinned_allocator_spsc`) is mapped straight into the `io_uring` kernel space, allowing the kernel to drop arriving packets perfectly into pre-allocated C++ structs without any user-space copying.
- **Cooperative Task Routing**: Utilizes `IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN` to prevent the kernel from unnecessarily interrupting the application thread with signals.

## 3. Bibliography & Proofs
1. **Axboe, J.** (2019). *Efficient IO with io_uring*. Linux Kernel Engineering Documentation.
2. **Corbet, J.** (2019). *Ringing in a new asynchronous I/O API*. LWN.net.
3. **Silberschatz, A., Galvin, P. B., & Gagne, G.** (2018). *Operating System Concepts*. (Context switch latency and Kernel-User boundary crossings).