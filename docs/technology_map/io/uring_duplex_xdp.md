# SlabFlux I/O: uring_duplex_xdp (`slabflux/io/uring_duplex_xdp.hpp`)

## 1. Architectural Justification
The `uring_duplex_xdp` component represents a hybrid execution matrix: it combines the zero-copy, bare-metal reception rings of `AF_XDP` with the asynchronous control-plane orchestration of `io_uring`. This bidirectional (duplex) engine is designed for edge-gateways that require absolute kernel-bypass for the hot data path, while simultaneously managing complex asynchronous control signals without blocking.

## 2. Hardware Implementation Directives
- **UMEM Matrix Monopolization**: Manages the `RX`, `TX`, `FILL`, and `COMPLETION` rings of the AF_XDP socket over a single cache-aligned physical memory block (UMEM).
- **Integrated io_uring Polling**: Instead of relying on a 100% CPU spin-loop that exhausts silicon thermal limits, the `uring_duplex_xdp` can seamlessly inject the XDP socket file descriptor into an `io_uring` instance via `IORING_OP_POLL_ADD`. This enables the hardware thread to sleep and wake up in single-digit microseconds upon NIC DMA completion, which is critical for secondary/backup nodes.
- **Zero-Copy Turnaround**: Payloads received on the RX ring are parsed using SIMD intrinsics. If the gateway needs to reflect the packet (e.g., an immediate hardware NACK), the memory is mutated in-place and instantly linked into the TX ring. The NIC pushes the frame back out onto the wire without the payload ever leaving the L1 cache.

## 3. Multi-Vector Conduit Handoff
Because it is fully duplex, the component operates a paired set of `spsc_conduit` matrices:
1. **Ingress Vector**: Pushes parsed `wire_frame` or `sovereign_signal` objects inbound to the deterministic execution core.
2. **Egress Vector**: Consumes generated network state outbound, linking the pointers directly to XDP TX descriptors for line-rate transmission.