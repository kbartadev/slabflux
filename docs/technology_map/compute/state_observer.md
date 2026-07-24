# SlabFlux Compute: State Observer & Snapshot Manager (`state_observer.hpp`)

## 1. Architectural Overview
The `snapshot_manager` handles the persistence and telemetry projection of the core computing logic. It guarantees that the hot path can backup its massive internal state matrix to NVMe storage without ever blocking, locking, or yielding the CPU to the OS scheduler.

## 2. Double-Buffered Shadow State
Writing directly from the live state matrix to disk is dangerous, as the hot path continues to mutate the data during the I/O transfer, resulting in a "torn" or corrupted backup.

To achieve point-in-time consistency:
1. The manager allocates a `shadow_state_` buffer on the exact same NUMA node as the hot path.
2. When a snapshot is requested, the manager executes a bit-perfect `__builtin_memcpy` of the live state to the shadow buffer. Utilizing AVX-512, copying a multi-megabyte state matrix takes less than 10 microseconds.
3. Once cloned, the live matrix is immediately freed to process the next network tick.

## 3. Zero-Syscall Persistence (`io_uring`)
Standard POSIX `write()` or `pwrite()` calls trigger context switches into Kernel mode, introducing non-deterministic latency spikes.

The `snapshot_manager` utilizes Linux `io_uring`:
- It prepares asynchronous Submission Queue Entries (SQEs) mapping the shadow buffer via `iovec`.
- It uses `io_uring_submit` to notify the kernel asynchronously.
- To prevent overlapping writes, it checks the Completion Queue (CQE) using `io_uring_peek_cqe`. If a previous snapshot is still in-flight to the NVMe drive, subsequent backup requests are deterministically dropped rather than stalling the queue.

## 4. Hardware I/O Alignment (`O_DIRECT`)
The file descriptor is opened with `O_DIRECT`, bypassing the Linux Kernel Page Cache. 
To satisfy NVMe block-alignment requirements:
- The internal header (`header_block_`) is strictly padded using `alignas(4096)`.
- The `shadow_state_` memory allocation uses `std::aligned_alloc` bounded to 4KB multiples.
This ensures the PCIe DMA controller can stream the memory directly from the RAM die to the NVMe flash cells without intermediate kernel bouncing.