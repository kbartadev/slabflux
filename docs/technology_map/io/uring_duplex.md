# SlabFlux I/O: io_uring Duplex (`slabflux/io/uring_duplex.hpp`)

## 1. Architectural Justification
The `uring_duplex` component unifies ingress and egress networking paths into a single multiplexed `io_uring` ring. This is crucial for applications that must aggressively minimize file descriptor overhead while maintaining zero-syscall networking.

## 2. Hardware Implementation Directives
- **Unified SQ/CQ Polling**: Both `IORING_OP_RECV` and `IORING_OP_SEND` entries share the same Submission/Completion queues, allowing a single SQPOLL kernel thread to manage bidirectional traffic.
- **Bidirectional Provided Buffers**: Memory blocks mapped via `IORING_REGISTER_BUFFERS` are dynamically rotated between receive and transmit operations, achieving a closed-loop zero-allocation state.

## 3. Backpressure Integration
If the transmit queue backs up, the duplex engine automatically throttles `IORING_OP_RECV` linking, ensuring the deterministic core is never overwhelmed by unequal network throughput.