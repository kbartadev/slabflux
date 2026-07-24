# Foundation: Audit Ledger (`slabflux/sys/audit_ledger.hpp`)

## 1. Architectural Justification
The `audit_ledger` is an append-only, non-blocking diagnostic memory region designed for high-speed internal fault tracking. It allows the deterministic engine to log severe anomalies (e.g., SIMD lane divergences) without invoking standard library loggers or kernel I/O wait states that induce micro-stalls.

## 2. Hardware Implementation Directives
- **Strict Memory Alignment**: The `audit_entry` is padded to exactly 128 bytes `alignas(std::hardware_constructive_interference_size * 2)`. This perfectly maps every record across exactly two cache lines, entirely preventing False Sharing and Write-Tearing between the generating thread and the telemetry observer thread.
- **Direct DAX Flush (`_mm_clwb`)**: The `ledger_append_allocator` issues `_mm_clwb` (Cache Line Write Back) immediately after writing the record, forcing the specific cache lines from L1 directly out to Persistent Memory (PMEM) or NVMe without issuing a kernel write syscall.
- **SIMD-Accelerated O(1) Traversal**: Utilizes AVX-512 Hardware Gather (`_mm512_i64gather_epi64`) and parallel threshold comparisons (`_mm512_cmpge_epu64_mask`) to identify the exact byte offset using Trailing Zero Count (`std::countr_zero`), instantly traversing massive diagnostic structures.

## 3. Bibliography & Proofs
1. **Intel Corporation**. *Intel Architecture Instruction Set Extensions Programming Reference*. (CLWB, Gather, and TZCNT instructions).
2. **Scargall, Clive**. (2020). *Programming Persistent Memory*. Apress. (DAX memory-mapped file persistence and cache-line writeback strategies).
3. **Mckenney, P. E.** (2017). *Is Parallel Programming Hard, And, If So, What Can You Do About It?* (False sharing mitigation on diagnostic structures).