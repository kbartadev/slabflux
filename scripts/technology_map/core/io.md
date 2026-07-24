# Blueprint: Core I/O & Hardware Streaming

## Architectural Overview
The I/O module guarantees zero-copy, cache-bypassing data transfer between the CPU and hardware peripherals (NVMe/NICs) to preserve L1-L3 cache residency for the trading/compute logic.

## Core Components
- **Non-Temporal Streaming (`non_temporal_writer.hpp`)**: Bypasses the CPU cache hierarchy completely by emitting `_mm512_stream_si512` and `_mm256_stream_si256` intrinsics. Data is streamed directly to main memory or PCIe devices, eliminating cache pollution during payload transmission.
- **Buffer Flushing (`buffer_flush.hpp`)**: Enforces absolute memory consistency by draining hardware Line Fill Buffers (LFB) via `_mm_sfence` combined with serialized locked instructions. This guarantees that asynchronous state updates reach visibility endpoints globally.
- **Eternal Memory (`eternal_memory.hpp`)**: Uses `_mm_clwb` (Cache Line Write Back) to force specific memory ranges into the persistence domain (e.g., NVMe or NVDIMM) at hardware speeds, avoiding heavy kernel system calls like `fsync`.
- **Endian Math (`endian.hpp`)**: Bypasses standard library scalar byte-swaps, utilizing 1-cycle AVX2 `_mm_shuffle_epi8` masks to seamlessly translate 16-byte protocol windows from network to host order dynamically.