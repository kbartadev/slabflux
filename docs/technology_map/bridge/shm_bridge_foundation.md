# Foundation: IPC Shared Memory Bridge (`slabflux/bridge/shm_bridge.hpp`)

## 1. Architectural Justification
The IPC Shared Memory Bridge provides ultra-low latency, zero-copy, lock-free Inter-Process Communication (IPC). It neutralizes the context-switching and socket-buffer limitations of standard Unix Domain Sockets and Pipes by mapping wait-free ring buffers directly across OS process boundaries.

## 2. Hardware Implementation Directives
- **Sovereign Cache-Line Geometry**: Physically separates the Control Block (atomic read/write cursors) from the Payload Block by exact cache-line dimensions (`alignas(128)`). This eliminates False Sharing between the writing process and reading process.
- **Non-Temporal Payload Siphoning**: Forces data into the physical RAM banks backing the `/dev/shm` segment using `_mm512_stream_si512`. This guarantees that blasting telemetry data across the IPC boundary does not evict the primary execution context from the transmitting CPU's L1 cache.
- **ASLR Neutralization (`shm_arena_duplex`)**: Intercepts absolute `T*` memory addresses and translates them into relative offsets to survive Address Space Layout Randomization (ASLR) mismatches between disparate processes mapping the identical SHM file.

## 3. Bibliography & Proofs
1. **Drepper, U.** (2007). *What Every Programmer Should Know About Memory*. Red Hat, Inc. (Shared Memory architectures and hardware cache alignment).
2. **Intel Corporation**. *Intel 64 and IA-32 Architectures Optimization Reference Manual*. (Non-Temporal Store directives and Write-Combining Buffers).
3. **Kemerlis, R. B., et al.** (2014). *ret2dir: Rethinking Kernel Isolation*. USENIX Security Symposium. (ASLR mitigations and shared memory vectors).