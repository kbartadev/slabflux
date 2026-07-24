# SlabFlux I/O: io_uring Ingress XDP (`slabflux/io/uring_ingress_xdp.hpp`)

## 1. Architectural Justification
Combines the extreme low-latency benefits of `AF_XDP` with the asynchronous notification model of `io_uring`. This hybrid approach is designed for standby or edge nodes that require microsecond wakeups but cannot afford to spin-lock a CPU core at 100% utilization.

## 2. Hardware Implementation Directives
- **POLL_ADD Wakeups**: The `AF_XDP` socket file descriptor is registered with `io_uring` using `IORING_OP_POLL_ADD`. The CPU can safely sleep (C-states) until a packet arrives.
- **Instant Extraction**: Upon wakeup via the Completion Queue (CQE), the engine instantly switches to raw `AF_XDP` ring consumption, fetching the UMEM payload pointers.

## 3. Deterministic Handoff
After awakening, it processes bursts of packets continuously until the RX ring is empty, handing off wrapped pointers to the zero-allocation conduit before sleeping again.