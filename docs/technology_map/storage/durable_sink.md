# SlabFlux Storage: Durable Sink (`durable_sink.hpp`)

## 1. Architectural Overview
Standard enterprise logging or data persistence relies on synchronous `write()` syscalls or background sleeper threads using condition variables. In a deterministic execution pipeline, a disk stall is catastrophic.
The `durable_sink` physically decouples NVMe persistence from the CPU execution vector, providing an asynchronous, Kernel-Bypass persistence terminal.

## 2. SQPOLL Background Submission
The `durable_sink` connects directly to the execution pipeline as a terminal (leaf) handler. 
When an event requires persistence:
1. It copies the aligned struct properties directly into the `io_uring` Submission Queue.
2. By leveraging the `IORING_SETUP_SQPOLL` flag, the Linux Kernel spawns a dedicated polling thread that actively consumes the Submission Queue.
3. The SlabFlux application never executes a system call to notify the kernel, eradicating context-switch overhead completely.

## 3. O(1) Hot-Path Release
Because the sink operates strictly via memory-mapped circular queues shared with the kernel:
- Submitting an I/O request is purely a user-space integer increment and an `_mm_sfence()`.
- The sink releases control back to the dispatch thread in under 15 nanoseconds, well before the physical hardware flush resolves on the NAND flash blocks.

## 4. Crash Consistency
If the system violently crashes before the kernel commits the SQPOLL entries to disk, standard asynchronous I/O would lose the data. 
To mitigate this, the `durable_sink` integrates with the `wire_frame_lsn` sequencing. A cluster failover will not acknowledge the missing sequence IDs, forcing the authoritative bridge to seamlessly replay the dropped frames from redundant peers.