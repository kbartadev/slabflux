# SlabFlux Sys: Audit Ledger (`audit_ledger.hpp`)

## 1. Architectural Overview
The `audit_ledger` is an append-only, non-blocking diagnostic memory region designed for high-speed internal fault tracking. It allows the deterministic engine to log severe anomalies (e.g., SIMD lane divergences) without invoking standard library loggers or kernel I/O wait states.

## 2. Strict Memory Alignment
The `audit_entry` structure is strictly padded to 128 bytes `alignas(std::hardware_constructive_interference_size)`. This perfectly maps every record across exactly two cache lines, entirely preventing False Sharing and Write-Tearing between the generating thread and the telemetry observer thread.

## 3. Direct DAX Flush (`_mm_clwb`)
To ensure audit entries are preserved even during a hard CPU lockup, the `ledger_append_allocator` issues `_mm_clwb` (Cache Line Write Back) immediately after writing the record. This forces the specific cache lines from L1 directly out to Persistent Memory (PMEM) or NVMe without issuing a kernel write syscall.

## 4. SIMD-Accelerated O(1) Traversal
When the system or a management thread needs to locate a specific LSN in a massive ledger, scalar scanning is too slow.
- The ledger utilizes AVX-512 Hardware Gather (`_mm512_i64gather_epi64`).
- It fetches 8 non-contiguous 64-bit LSN offsets simultaneously.
- It executes a parallel threshold comparison (`_mm512_cmpge_epu64_mask`) and identifies the exact byte offset using Trailing Zero Count (`std::countr_zero`). 
This provides an instantaneous O(1) search across massive diagnostic structures.