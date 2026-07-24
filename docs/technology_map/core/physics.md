# SlabFlux Core: Physics & Cache Sovereignty (`physics.hpp`)

## 1. Architectural Overview
Standard memory movement via `std::memcpy` pulls data through the L1/L2 cache hierarchy. For "write-once, read-rarely" artifacts (like archiving completed orders or flushing telemetry), this brutally evicts the active market data states out of the L1 cache, causing massive latency spikes.

## 2. Non-Temporal Streaming
The `non_temporal_stream` implements zero-overhead C++20 `std::span` wrappers around architecture-specific streaming instructions.
- It utilizes `_mm512_stream_si512` (AVX-512) and `_mm256_stream_si256` (AVX2).
- These instructions completely bypass the L1, L2, and L3 caches, writing the data directly into the Write-Combining Buffers (WCB) of the CPU, which then flush directly to physical DRAM.

## 3. Fault Protection & Alignment
Non-temporal stores will trigger immediate General Protection Faults (Segfaults) if the memory address is not strictly aligned to 64 bytes (AVX-512) or 32 bytes (AVX2).
The engine employs a bitwise mask `(reinterpret_cast<uintptr_t>(dst_bytes) & 63) == 0` to physically prove alignment on the hot path. If unaligned, it seamlessly falls back to standard copy operations.

## 4. Forced Memory Visibility
Every block of non-temporal writes concludes with a mandatory `_mm_sfence()`. This instruction drains the Write-Combining Buffers, ensuring that asynchronous reader threads on remote NUMA nodes can immediately witness the structural changes in global RAM.