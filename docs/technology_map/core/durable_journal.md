# SlabFlux Core: Durable Journal (`durable_journal.hpp`)

## 1. Architectural Overview
The Durable Journal is an ultra-low latency Write-Ahead Log (WAL) designed to map causal state changes directly to NVMe block devices without stalling the compute hot-path.

## 2. Page Cache Bypassing (`O_DIRECT`)
Standard file I/O traverses the Linux Virtual File System (VFS) and pollutes the OS Page Cache.
- The Journal opens block devices strictly with `O_DIRECT`.
- This ensures that data is streamed via DMA straight from the application's pinned Memory Arenas to the NVMe controller, preserving L1/L2 cache residency for the AI and Compute matrices.

## 3. Asynchronous Ring Submission
Uses `io_uring` exclusively for persistence. 
- When the active environment pushes a batch of `wire_frame_lsn` events, the journal immediately registers them in the Submission Queue (SQ) and returns execution to the caller.
- Hardware completion interrupts are handled seamlessly by the kernel polling thread, completely decoupling network-speed execution from physical flash-write latencies.