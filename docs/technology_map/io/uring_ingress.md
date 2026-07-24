# SlabFlux I/O: uring_ingress (`slabflux/io/uring_ingress.hpp`)

## 1. Architectural Justification
The `uring_ingress` component handles the high-frequency ingestion of connectionless (UDP) datagrams using the `io_uring` interface. It provides an optimal balance between standard POSIX socket multiplexing and absolute bare-metal `AF_XDP`, allowing shared network interfaces to achieve kernel-bypass-like latencies without monopolizing the NIC hardware.

## 2. Hardware Implementation Directives
- **Multishot Reception**: Utilizes `IORING_OP_RECVMSG` with the `IORING_RECV_MULTISHOT` flag. This allows a single Submission Queue Entry (SQE) to continuously yield Completion Queue Entries (CQEs) as new datagrams arrive, radically amortizing the SQ lock and kernel submission overhead.
- **Buffer Ring Pre-Allocation**: Relies on `io_uring`'s Provided Buffers feature (`IORING_OP_PROVIDE_BUFFERS` or ring-mapped buffers). The kernel DMAs incoming packets directly into pre-registered SlabFlux `managed_data` memory blocks, completely avoiding `malloc` or `sk_buff` payload duplication.
- **Wait-Free Polling**: CQEs are harvested by a dedicated spin-loop thread. Once a datagram is validated, its physical memory pointer is immediately injected into the execution manifold's `spsc_conduit`.

## 3. Algorithmic Handoff
Because UDP datagram boundaries are guaranteed by the hardware, the `uring_ingress` module bypasses TCP stream reassembly entirely. Network frames are sliced in-place using SIMD intrinsics. If a structural match is found, the verified boundaries are dispatched as atomic events to the deterministic core.