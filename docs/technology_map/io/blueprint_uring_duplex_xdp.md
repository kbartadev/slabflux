# Blueprint: uring_duplex_xdp.hpp

## Architectural Overview
Hybrid bidirectional gateway combining the absolute kernel-bypass reception of `AF_XDP` with the asynchronous control-plane orchestration of `io_uring` to achieve extreme edge-gateway performance without blocking.