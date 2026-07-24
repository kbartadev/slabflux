# Blueprint: uring_ingress_xdp.hpp

## Architectural Overview
Hybrid power-optimized reception module for standby nodes. Combines `AF_XDP` memory mapping with `io_uring` polling (`IORING_OP_POLL_ADD`) to achieve microsecond wakeups without 100% CPU spin-locking.