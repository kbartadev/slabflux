# SlabFlux Core: Core I/O (`io.hpp`)

## 1. Architectural Overview
The bare-metal ingress and egress orchestration layer, managing asynchronous buffer transfers exclusively via kernel-bypass paradigms.

## 2. io_uring Orchestration
Leverages the `io_uring` kernel API configured with `IORING_SETUP_SQPOLL`. It establishes submission and completion rings that are polled entirely from userspace through mapped shared memory.

## 3. The Zero-Syscall Boundary
Eliminates the `epoll` boundary completely. Because the kernel threads actively monitor the ring, the SlabFlux application never drops into kernel space to fetch a packet or append data to a socket, driving theoretical gigabit line rates directly into the C++ matrix.