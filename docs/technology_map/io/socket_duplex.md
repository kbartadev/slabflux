# SlabFlux I/O: Socket Duplex (`slabflux/io/socket_duplex.hpp`)

## 1. Architectural Justification
The `socket_duplex` component serves as a unified, bidirectional POSIX socket wrapper. For applications not utilizing kernel-bypass (like AF_XDP or DPDK), standard POSIX sockets are often the bottleneck. This module mitigates that by wrapping standard OS networking into the SlabFlux wait-free execution matrices using strict `epoll` edge-triggered state machines.

## 2. Hardware Implementation Directives
- **Edge-Triggered Multiplexing**: Utilizes `EPOLLET` (Edge-Triggered) mode to ensure the kernel only wakes up the application once per data state change, drastically reducing system call overhead compared to level-triggered polling.
- **Batched Non-Blocking I/O**: Performs `recv()` and `send()` operations in greedy loops until `EAGAIN` / `EWOULDBLOCK` is hit. This ensures that every wakeup clears the NIC buffers to the maximum extent.
- **Zero-Allocation Routing**: Binds directly into pre-allocated `spsc_conduit` structures. The incoming byte stream is mapped into contiguous buffer pools without invoking `malloc()`, eliminating heap fragmentation and allocation latency.

## 3. Pipeline Integration
Sits at the edge of the user-space boundary. It ingests bytes from the OS network stack and pushes them into the deterministic compute core via SPSC rings, while simultaneously draining outbound state events and executing vectorized writes (`writev()`) to push them back to the wire.