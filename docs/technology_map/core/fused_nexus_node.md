# SlabFlux Core: Fused Nexus Node (`fused_nexus_node.hpp`)

## 1. Architectural Overview
The `fused_nexus_node` serves as the ultra-low latency gateway between the Linux kernel's network stack and SlabFlux's deterministic Shared Memory (SHM) frame buffers. By fusing together Multishot Buffer Rings and SQPOLL threads, it completely eradicates context-switch overhead during network ingestion.

## 2. Zero-Syscall Polling
Traditional network I/O requires continuous `epoll()` or `recv()` system calls, destroying deterministic cycle budgets.
- The Nexus configures `io_uring` with `IORING_SETUP_SQPOLL | IORING_SETUP_SQ_AFF`.
- The Linux kernel spins a dedicated kernel thread pinned to an isolated CPU core.
- This kernel thread continuously polls the Submission Queue (SQ) ring buffer entirely via shared memory, completely removing user-kernel boundary transitions for the application.

## 3. Multishot Buffer Rings (`io_uring_buf_ring`)
- The Nexus maps the DMA memory pool (such as `pinned_allocator_spsc`) directly into the `io_uring` kernel space.
- Arriving network packets are written directly into pre-allocated C++ `wire_frame` structs by the NIC/kernel without any intermediate user-space copying.
- Multishot receive requests (`IORING_OP_RECV_MULTISHOT`) allow a single SQE to generate multiple Completion Queue Events (CQEs) as data streams in, achieving millions of messages per second with minimal SQ replenishment.

## 4. Cooperative Task Routing
- Utilizes `IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN` to optimize CPU scheduling.
- Prevents the kernel from unnecessarily interrupting the hot-path application thread with inter-processor interrupts (IPIs) or POSIX signals.