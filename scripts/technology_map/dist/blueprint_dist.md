# Blueprint: Distributed Mesh Architecture

## Architectural Overview
The distributed layer (`slabflux::dist` and `slabflux::net`) guarantees sub-microsecond state synchronization across multiple physical nodes. It bypasses standard TCP/IP network stacks via RDMA and wait-free routing mechanisms to treat a cluster of machines as a singular memory space.

## Core Components
- **Wait-Free Routing Fabric**: Multiplexes outbound state frames across discrete network interfaces using atomic epoch tracking, bypassing global lock contention on the physical network cards.
- **RDMA Memory Windows**: Registers mapped `mmap` slabs directly with the InfiniBand/RoCE Host Channel Adapter (HCA), allowing remote nodes to push state updates directly into the local CPU's L3 cache without interrupting the OS.
- **Causal Mesh Engine**: Ensures globally ordered, side-effect-free execution barriers across distributed nodes by exchanging and validating Log Sequence Number (LSN) vectors before committing distributed transactions.
- **Reliable Multicast Matrix**: Implements a jitter-free UDP multicast rebroadcast mechanism using NAK-based (Negative Acknowledgment) packet recovery, designed for high-frequency trading data distribution.