# SlabFlux I/O: Shared Memory Nexus (`slabflux/io/shm.hpp`)

## 1. Architectural Justification
The SHM Nexus acts as an ultra-low-latency Inter-Process Communication (IPC) matrix. It allows decoupled SlabFlux applications (e.g., market data feed handlers and strategy execution cores) to exchange state pointers instantly without routing traffic through the loopback network stack.

## 2. Hardware Implementation Directives
- **POSIX mmap Alignment**: Utilizes `shm_open` and `mmap` to map contiguous virtual memory pages into identical physical RAM blocks across different processes.
- **Lock-Free Conduits**: Wraps standard `spsc_conduit` topologies within the shared memory block, allowing inter-process wait-free queueing at identical speeds to intra-process thread queueing.
- **Hardware Cache-Line Adherence**: Ensures identical virtual-to-physical alignment so that `__atomic` variables execute perfectly inside identical CPU L1 cache zones.

## 3. Integration in the DAG
Serves as the physical transport layer for the `shm_bridge`. It transparently replaces internal `malloc` allocations for conduit rings with shared memory mappings, ensuring inter-process state handoffs exhibit zero additional overhead compared to standard inter-thread communication.