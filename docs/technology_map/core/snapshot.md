# SlabFlux Core: Snapshot Engine (`snapshot.hpp` / `snapshot_engine.hpp`)

## 1. Architectural Overview
Orchestrates point-in-time state capture across the entire cluster, flushing massive datasets (gigabytes of RAM) directly to NVMe storage without introducing latency stalls on the execution hot path.

## 2. Shadow State Cloning
Leverages a double-buffered architecture. AVX-512 `__builtin_memcpy` clones the live compute state into a pre-allocated shadow buffer in under 10 microseconds.

## 3. Asynchronous Kernel Offload
The persistence layer utilizes `io_uring` with `O_DIRECT` to asynchronously flush the shadow buffer to block devices. It entirely bypasses the Linux Virtual File System (VFS) Page Cache, maintaining line-rate IO streams directly from memory to flash.