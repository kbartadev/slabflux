# SlabFlux Bridge: IPC Shared Memory Bridge (`shm_bridge.hpp`, `shm_arena_duplex.hpp`)

## 1. Architectural Overview
The IPC Shared Memory Bridge provides ultra-low latency, zero-copy, lock-free Inter-Process Communication (IPC). It completely bypasses standard Unix sockets and pipes by mapping wait-free ring buffers directly across OS process boundaries.

## 2. Sovereign Cache-Line Geometry
The bridge maps `/dev/shm` (POSIX Shared Memory) files into the virtual address spaces of distinct processes.
- **Control vs. Payload Separation**: The Control Block (containing the atomic read/write ingress and egress cursors) is physically separated from the Payload Block by exact cache-line dimensions (usually 64 or 128 bytes). 
- This architecture prevents false sharing between Process A (the Creator) and Process B (the Joiner), ensuring that updating a cursor does not invalidate the cache line containing the data payload on a different physical CPU socket.

## 3. ASLR Neutralization (`shm_arena_duplex`)
Because Linux enforces Address Space Layout Randomization (ASLR), mapping the same shared memory file in two different processes will almost certainly result in two different base virtual memory addresses.

To safely transfer linked structures (like strings or trees) across this gap:
- **Relative Offset Translation**: The duplex arena automatically intercepts any absolute `T*` memory addresses and translates them into integer offsets relative to the base address of the shared segment before transmission.
- **Zero-Cost Deserialization**: Upon reading the payload, the receiving process reconstructs the absolute pointers by adding the local ASLR-shifted base address to the received offset, ensuring memory safety without complex serialization frameworks.

## 4. Hardware Streaming Optimization
For extremely small telemetry payloads (under 256 bytes), the bridge implements aggressive L1/L2 cache bypassing:
- It natively compresses payload properties into SIMD registers (`__m512i`).
- It forcefully writes this data directly to the RAM banks backing the shared memory segment using non-temporal streams (`_mm512_stream_si512`).
- This guarantees that IPC communication does not evict the primary execution context (like the local Order Book) from the hot-path processor's L1 cache.