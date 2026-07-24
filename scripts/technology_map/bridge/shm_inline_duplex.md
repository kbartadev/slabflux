# SlabFlux Bridge: SHM Inline Duplex (`shm_inline_duplex.hpp`)

## 1. Architectural Overview
The `shm_inline_duplex` module optimizes high-frequency, small-payload Inter-Process Communication (IPC) across shared memory (SHM) segments. It is specifically engineered to transfer structs under 256 bytes without destroying the L1 data cache of the transmitting processor.

## 2. L1 Cache Sovereign Siphoning
Standard memory copies (`std::memcpy`) load the destination memory address into the L1 cache before writing to it. When transferring data to another process via SHM, polluting the local L1 cache with outbound IPC data evicts the critical context (like the local Order Book or AI weights) that the hot path needs for its next operation.

The Inline Duplex neutralizes this cache poisoning:
- It utilizes SIMD `__m512i` registers to aggregate the outbound struct properties natively.
- It executes **Non-Temporal Stream** instructions (`_mm512_stream_si512`) to forcefully blast the data directly to the physical RAM banks backing the shared memory segment.
- This ensures the data reaches the receiving process across the PCIe/memory bus while leaving the transmitting core's L1/L2 caches completely untouched.

## 3. Zero-Copy Inlining
Rather than allocating intermediate node structures to track data over the bridge (the standard pointer-moving bridge pattern), the `shm_inline_duplex` relies on raw byte-copies of `alignas(64)` structs physically embedded into the shared matrix slots. 
This flattens the memory layout and provides extreme spatial locality for the receiving process.

## 4. Application
This module is heavily utilized by the `telemetry_node` and `management_nanoscope`. It allows the trading and AI cores to blast millions of sub-microsecond diagnostic metrics into external visualization processes without degrading deterministic trading latencies.