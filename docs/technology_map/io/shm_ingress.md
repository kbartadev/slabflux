# SlabFlux I/O: SHM Ingress (`slabflux/io/shm_ingress.hpp`)

## 1. Architectural Justification
Acts as the reception half of the shared memory IPC framework. It continually polls a memory-mapped SPSC ring to detect incoming state transitions from external publisher processes.

## 2. Hardware Implementation Directives
- **Cache-Line Polling**: Spins on a designated `alignas(64)` atomic tail pointer residing in the `mmap` block.
- **Zero-Copy Extraction**: Reads the raw byte offset directly from the shared queue and projects it into a `sovereign_signal` envelope without local buffer duplication.

## 3. Pipeline Integration
Functions as a drop-in replacement for network ingress modules (like `io_uring_ingress`). It injects external IPC messages directly into the deterministic DAG for routing and business logic processing.