# SlabFlux Core: Pipeline Lane (`pipeline_lane.hpp`)

## 1. Architectural Overview
The `pipeline_lane` establishes the isolated, cache-friendly execution boundaries for the TCP Stream Defragmenter and high-level routing matrix. It perfectly maps a protocol parser, connection state registry, and business logic pipeline into a unified, zero-contention execution trace.

## 2. Execution Traces

### Vector Stream Processing (`execute_vector_stream`)
When a node processes a high volume of small protocol messages (e.g., FIX payloads), decoding them individually limits throughput.
- The lane queries the topology router for a batch of `tagged_pointer` envelopes.
- It groups the inbound pointers and executes them 4-at-a-time natively via AVX2 (`_mm256_load_si256` and `dispatch_simd`).
- This allows the backend `demuxer` to process structural validations in parallel across the 256-bit registers before executing the scalar business logic.

### Scalar Downstream Batching (`execute_tick`)
For standard event payloads where vectorization is non-viable or the state requires strict serialization:
- The lane consumes the batch from the ingress conduit.
- It utilizes `#pragma GCC unroll 32` to mathematically flatten the processing loop.
- This eliminates branch prediction overhead entirely across the entire batch boundary, resolving up to 32 network frames in a single contiguous block of machine code.

## 3. Session State Sovereignty
In standard web servers, connection state is stored in global Hash Maps guarded by `std::shared_mutex`. 
The `pipeline_lane` physically embeds the `session_storage_registry` directly into its own memory layout (`alignas(64)`). 
Because the lane is pinned to a specific thread via the `hardware_topology`, session storage looks-ups are perfectly lock-free and guarantee 100% L1 Data Cache residency, ensuring zero latency variance during connection handshakes.