# SlabFlux I/O: Windows RIO Duplex (`slabflux/io/rio_duplex.hpp`)

## 1. Architectural Justification
For environments constrained to Windows Server deployments, traditional Winsock calls introduce severe kernel transitions. The `rio_duplex` engine leverages the Windows Registered I/O (RIO) API to approximate Linux-style kernel-bypass performance.

## 2. Hardware Implementation Directives
- **Registered Buffer Rings**: Pre-registers continuous memory blocks with the Windows kernel via `RIORegisterBuffer`. 
- **Deferred Completions**: Avoids IOCP thread-pool switching by manually polling the `RIOCQ` (Completion Queue) directly from the application's spin-loop.
- **Zero-Copy Transmits**: Uses `RIOSend` mapped exactly to the payload constraints to prevent NDIS stack payload replication.

## 3. Pipeline Integration
Maps directly into the SlabFlux SPSC network conduits. Inbound RIO completions instantly push `wire_frame` pointers to the deterministic compute core, while the egress engine pushes outbound states directly into the RIO transmission rings, achieving zero-syscall duplex parity with Linux-based DPDK/io_uring implementations on Windows hosts.