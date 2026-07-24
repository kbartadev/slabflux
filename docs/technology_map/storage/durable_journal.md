# Foundation: Durable Journal (`slabflux/storage/durable_journal.hpp`)

## 1. Architectural Justification
The `durable_journal` provides an ultra-low latency, zero-copy Write-Ahead Log (WAL) to NVMe block devices. It is the persistence anchor that ensures no acknowledged event is ever lost, without forcing the hot path to yield to OS storage drivers or Page Caches.

## 2. Hardware Implementation Directives
- **Kernel-Bypass Storage**: Employs `io_uring` with `SQPOLL` enabled. A dedicated kernel thread actively polls the shared-memory submission queue, meaning the application never executes a `write()` system call.
- **O_DIRECT Alignment**: Files are opened with `O_DIRECT`, writing straight to the NVMe PCIe lanes and bypassing the Linux Page Cache entirely to prevent latency spikes during OS cache evictions.
- **Zero-Serialization Framing**: Strictly persists `wire_frame_lsn` payloads. Since these are `alignas(64)` PODs, they are dumped to disk via `memcpy` and SG-lists (`iovec`) natively without JSON or binary serialization loops.

## 3. Bibliography & Proofs
1. **Axboe, J.** (2019). *Efficient IO with io_uring*. Linux Kernel Engineering Documentation.
2. **Linux Kernel Organization**. *O_DIRECT documentation*. (Bypassing the VFS page cache for block storage).
3. **Mohan, C., et al.** (1992). *ARIES: A Transaction Recovery Method Supporting Fine-Granularity Locking and Partial Rollbacks Using Write-Ahead Logging*. ACM Transactions on Database Systems (TODS).