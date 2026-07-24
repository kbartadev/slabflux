# SlabFlux I/O: io_uring Stream (`slabflux/io/uring_stream.hpp`)

## 1. Architectural Justification
Manages connection-oriented TCP workloads. Unlike UDP, TCP requires stream windowing, fragmentation handling, and strict ordering. The `uring_stream` orchestrates this via `io_uring` multishot operations to prevent blocking the deterministic core.

## 2. Hardware Implementation Directives
- **Multishot Reception**: Utilizes `IORING_OP_RECV_MULTISHOT` to continually sink incoming TCP streams into pre-provided buffer rings.
- **Defragmentation Integration**: Tightly coupled with the `tcp_stream_defragmenter` to align fragmented payloads directly against DMA-mapped memory without intermediate copying.

## 3. Backpressure Handling
Executes internal window scaling by pausing specific SQE polling if the deterministic computation matrix reaches its internal queue capacity limits.