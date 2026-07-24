# Blueprint: uring_ingress.hpp

## Architectural Overview
High-frequency UDP datagram ingestion using `io_uring` multishot reception (`IORING_RECV_MULTISHOT`). Combines Provided Buffers and wait-free polling for near-kernel-bypass latencies.