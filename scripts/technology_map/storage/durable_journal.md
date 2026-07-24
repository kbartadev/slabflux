# SlabFlux Storage: Durable Journal (`durable_journal.hpp`)

## 1. Architectural Overview
The `durable_journal` provides an ultra-low latency, zero-copy Write-Ahead Log (WAL) to NVMe block devices. It is the persistence anchor that ensures no acknowledged event is ever lost, without forcing the hot path to yield to OS storage drivers.

## 2. Kernel-Bypass Storage (io_uring)
Writing to disks typically involves deep kernel context switches and page-cache thrashing.
The `durable_journal` circumvents this entirely:
- It utilizes `io_uring` with `SQPOLL` enabled. A dedicated kernel thread actively polls the shared-memory submission queue, meaning the SlabFlux application never executes a `write()` system call.
- The files are opened with `O_DIRECT`, writing straight to the NVMe PCIe lanes and bypassing the Linux Page Cache entirely.

## 3. Payload Serialization
The journal strictly persists `wire_frame_lsn` payloads. 
- Since these payloads are strictly POD and `alignas(64)`, there is zero serialization overhead.
- The journal employs a multi-megabyte staging buffer mapped using `MAP_HUGETLB`. As events stream through the system, they are `memcpy`'d into the staging area using SIMD instructions.
- When a block fills, or a sequence marker triggers a commit, the `io_uring` engine flushes the fixed buffer to disk.

## 4. Fault-Tolerant Replay Integration
Upon system boot, the `durable_source` maps the journal files back into memory. The `replay_manager` scans the headers and chronologically injects the historical `wire_frame_lsn` events back into the `pipeline`, priming the logic engine and caches to their exact state before the crash.