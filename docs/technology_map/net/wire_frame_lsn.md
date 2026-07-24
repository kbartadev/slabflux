# SlabFlux Net: Wire Frame LSN (`slabflux/net/wire_frame_lsn.hpp`)

## 1. Architectural Overview
In deterministic distributed systems, correct ordering of events is prioritized above real-time clocks. The `wire_frame_lsn` serves as the chronological envelope for causal routing, embedding Logical Sequence Numbers (LSNs) directly into raw network payloads.

## 2. Structural Directives
- **Zero-Cycle Embedding**: The LSN and vector clock metadata are physically positioned at the absolute head of the geometric memory span. This allows the deterministic core to validate chronological sequence using a single 64-bit integer comparison.
- **Continuous State Propagation**: As packets move through the execution DAG, the LSN acts as the absolute arbiter of state mutations. If a frame arrives with an out-of-order LSN, the execution core immediately halts and requests retransmission.

## 3. False Sharing Prevention
The structure strictly adheres to `alignas(64)` boundaries. By ensuring the LSN and payload never span across multiple CPU cache lines, the system prevents cross-core False Sharing invalidations when the sequence number is atomicized.

## 4. Pipeline Integration
Acts as the primary state sequence tracker for the `replay_saga` and the `state_streamer`. It travels intimately with the raw network payload down the SPSC conduits, guaranteeing that the compute core processes distributed events in a mathematically irreproachable chronological order.