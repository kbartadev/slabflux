# SlabFlux I/O: io_uring Ingress (`io_uring_ingress.hpp`, `uring_ingress_stream.hpp`)

## 1. Architectural Overview
While AF_XDP provides absolute bare-metal performance, it requires specialized drivers and exclusive control of the NIC. The `io_uring_ingress` provides an extraordinarily high-performance alternative for standard Linux network stacks, leveraging the modern `io_uring` subsystem to achieve near-kernel-bypass latencies on generic POSIX sockets.

## 2. Zero-Syscall Network Polling
Standard `epoll` architectures require the application to call `epoll_wait()` to discover events, followed by `recv()` to extract the data—incurring two heavy kernel context switches per network tick.

The `io_uring_ingress` architecture neutralizes system calls entirely:
- **SQPOLL Thread**: The engine initializes the ring with the `IORING_SETUP_SQPOLL` flag. The Linux kernel dedicates an internal thread to actively poll the Submission Queue (SQ) residing in shared memory.
- **Continuous Submissions**: As soon as a packet is received, the application seamlessly links another `IORING_OP_RECV` request into the ring and advances the tail pointer. The kernel immediately processes it without the user-space thread ever executing a system call.

## 3. Fixed Buffers & Zero-Copy Alignment
To prevent the kernel from endlessly allocating and copying internal memory buffers (`sk_buff` payload replication):
- The module pre-allocates a massive array of aligned memory blocks from the `spsc_pool`.
- It registers these blocks permanently with the kernel using `IORING_REGISTER_BUFFERS`.
- When the NIC receives a packet, the kernel DMA engine deposits the payload directly into these pre-pinned user-space buffers, entirely avoiding the CPU-heavy `copy_to_user` trajectory.

## 4. Integration with SlabFlux Topology
Once the `io_uring_ingress` loop consumes a Completion Queue Entry (CQE) containing the network payload:
- It instantly wraps the buffer pointer in a `managed_data` or `wire_frame` structure.
- It pushes the pointer wait-free down a `spsc_conduit` directly to the `branchless_engine`, achieving end-to-end multi-threading without a single mutex.