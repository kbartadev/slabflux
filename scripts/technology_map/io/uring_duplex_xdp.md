# Blueprint: uring_duplex_xdp.hpp

## Architectural Overview
Coordinates full bi-directional AF_XDP interaction. Unifies four distinct lock-free rings (`rx`, `tx`, `fill`, `comp`) within a single execution block to control full NIC DMA boundaries.

## Core Logic & Mechanisms
- **Synchronized 4-Ring Polling**: Consumes `rx` events and simultaneously acknowledges `comp` transmissions, ensuring the network interface is perfectly balanced and never throttles.
- **Batch Vectorization**: Sweeps rings in predefined batch limits to preserve local L1/L2 cache heat and mitigate infinite loop stalls when traffic bursts asynchronously.
- **Strict Memory Ownership**: Binds strictly to `xsk_socket` structures, asserting definitive boundary constraints over the underlying Linux eBPF file descriptors.