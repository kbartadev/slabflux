# SlabFlux Sys: Blackbox Recorder (`blackbox_recorder.hpp`)

## 1. Architectural Overview
The `blackbox_recorder` is a circular flight-data memory system designed to preserve the exact micro-architectural state of the application leading up to a fatal crash, divergence, or panic. It operates entirely without system calls on the hot path, ensuring zero latency degradation during active tracing.

## 2. Lock-Free Circular Flight Data

### Contiguous Ring Buffer
The recorder uses a pre-allocated, HugePage-backed circular buffer. On every major state transition, the engine writes a highly compressed `fault_record` struct into the buffer.
- The buffer is bounded (e.g., tracking the last 65,536 events). 
- When the capacity is reached, it seamlessly wraps around and overwrites the oldest events without requiring reallocation or bounds-checking branches (enforced via power-of-two bitwise masking).

### Direct Memory Mapped Output
The backing memory is often mapped directly to `/dev/shm` (Shared Memory) or an `mmap`'d file on a fast NVMe drive. This ensures that even if the primary process violently segfaults or the kernel panics, the flight data remains physically intact in RAM or on disk.

## 3. Post-Mortem Forensics

When a critical invariant violation triggers the `error_arbiter`, the system halts and invokes the `blackbox_recorder`'s flush routine.

The flushed data provides an exact timeline of events, including:
- **Hardware Telemetry**: Ingress `TSC`, Logic Processing `TSC`, and Egress `TSC`, providing a nanosecond-accurate timeline of latency spikes.
- **Logical State**: The LSN and the 64-bit mathematical hash of the execution node's matrix prior to the crash.
- **Divergence Metrics**: Historical Mean Squared Error (MSE) and EMA (Exponential Moving Average) limits leading up to the threshold breach.

### External Extraction
Because the data is stored in standard shared memory, an external, isolated process (the Telemetry Node) can safely read and dump the CSV/JSON crash reports without interfering with the locked or crashed core.

## 4. Cache-Sovereign Tracing
To avoid thrashing the L1 Data Cache with write-only telemetry records, the `blackbox_recorder` heavily utilizes `_mm_stream_si64` or equivalent non-temporal instructions. This allows the CPU to stream the flight data directly to the memory controllers, keeping the critical L1 cache lines entirely dedicated to the active order book or AI models.