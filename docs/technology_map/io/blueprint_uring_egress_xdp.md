# Blueprint: uring_egress_xdp.hpp

## Architectural Overview
High-throughput transmission scheduler for `AF_XDP` sockets. Utilizes `io_uring` to orchestrate batched doorbell signaling, flushing the TX ring without synchronous `sendto()` system calls.