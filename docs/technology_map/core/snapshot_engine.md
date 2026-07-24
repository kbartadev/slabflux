# SlabFlux Core: Sovereign Snapshot Engine (`snapshot_engine.hpp`)

## 1. Architectural Overview
The `snapshot_manager` is tasked with persisting the entirety of the application's internal memory matrix (which can span hundreds of megabytes) to non-volatile NVMe storage without introducing latency stalls on the execution hot path.

## 2. Double-Buffered Shadow State
Standard disk writes block the executing thread until the data is fully flushed, making them unacceptable for deterministic systems. 
The snapshot engine utilizes a double-buffered shadow architecture:
- A secondary `shadow_buffer_` is pre-allocated on the local NUMA node using `MAP_HUGE_2MB` and locked into physical RAM.
- When a snapshot is requested, the engine executes a rapid SIMD `__builtin_memcpy` to clone the live state into the shadow buffer. 
- For an optimized AVX-512 machine, cloning a gigabyte of state takes microseconds, establishing the point-in-time boundary instantly.

## 3. Asynchronous `io_uring` Persistence
Once the state is securely held in the shadow buffer, the engine submits the transfer task to the kernel:
- It utilizes an `O_DIRECT` file descriptor to bypass the Linux Page Cache, streaming the data directly to the NVMe PCIe lanes.
- It constructs `iovec` scatter/gather arrays to prepend a 4KB-aligned `header_block_` (containing the Logical Sequence Number) to the state payload.
- The task is pushed to the `io_uring` submission queue. The primary engine immediately resumes processing network events while the kernel hardware DMA transfer completes in the background.

## 4. Non-Overlapping Submissions
If the system generates state mutations faster than the NVMe drive can persist them, the I/O queue will saturate.
- The engine probes the completion queue (`io_uring_peek_cqe`) before initiating a new snapshot. 
- If the previous snapshot is still in flight to the silicon, the engine deterministically drops the new snapshot request. This guarantees that I/O backpressure never slows down the logic processing cores.