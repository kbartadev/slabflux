# SlabFlux Core: Wire Frame LSN (`wire_frame_lsn.hpp`)

## 1. Architectural Overview
Envelopes all inter-node network traffic with causal Lamport timestamps and Logical Sequence Numbers (LSN), establishing the baseline for distributed causality.

## 2. Cache-Aligned Boundaries
The frame is explicitly padded to align with 64-byte L1 cache boundaries. 
- This allows pure `reinterpret_cast` parsing at zero CPU cost when pulling data directly off the NIC or io_uring buffers.
- Avoids misaligned loads, preventing performance stalling in the `vector_lane_engine`.

## 3. Causal Journal Anchor
Acts as the structural foundation for the `durable_journal`, providing the indexed headers required to identically rebuild historical state machines.