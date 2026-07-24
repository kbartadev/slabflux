# Blueprint: io_uring_durable_journal.hpp

## Architectural Overview
Provides strict non-blocking sequential persistence to NVMe disks. Implements an Event Sourcing sink utilizing kernel-bypass I/O to append state changes natively.

## Core Logic & Mechanisms
- **Slot Reservation**: `reserve_slot()` fetches an aligned write block mapped directly against the file's block size without blocking the caller.
- **Asynchronous Direct I/O**: Issues `IORING_OP_WRITE` payloads strictly via `O_DIRECT`, bypassing the kernel page cache completely to write explicitly to the physical disk media.
- **Completions Sweeper**: `poll_completions()` acts as an amortized cleanup mechanism, executing background logic without stalling the execution sequence.