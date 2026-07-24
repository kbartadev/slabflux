# Blueprint: uring_duplex_stream.hpp

## Architectural Overview
Governs bidirectional, connection-oriented workloads over `io_uring`, chaining `IORING_OP_RECV` and `IORING_OP_SEND` operations to manage streaming sockets and backpressure without blocking the application manifold.