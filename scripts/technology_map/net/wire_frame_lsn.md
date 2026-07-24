# SlabFlux Net: Wire Frame LSN (`wire_frame_lsn.hpp`)

## 1. Architectural Overview
The `wire_frame_lsn` is the supreme chronological envelope for all cluster-bound network traffic. Before any event leaves a node or enters the deterministic pipeline, it is enveloped in this structure to mathematically anchor it to the causal continuum.

## 2. Sequence Identity
- **`sequence_id`**: The absolute Logical Sequence Number (LSN). It guarantees strict monotonic progression across the cluster.
- **`origin_node_id`**: A unique 16-bit identifier for the broadcasting node.
- **`causal_state`**: An embedded Vector Clock tracking the timeline of all peer nodes.

## 3. Zero-Serialization Embedding
The envelope is a variadic template `wire_frame<T>`, meaning it physically wraps the generic payload `T` (e.g., `TradeTick`) inside its memory footprint.
- It guarantees cache-alignment (`alignas(64)`).
- When the network module receives this byte-array over TCP/UDP, it employs a `reinterpret_cast` directly to the `wire_frame_lsn` struct. Because the payload is physically inlined, parsing takes 0 CPU cycles.

## 4. Interaction with the Causal Sequencer
The `causal_ingress_router` intercepts this envelope. If the `sequence_id` is out-of-order relative to the node's local truth matrix, the frame is instantly quarantined in an O(1) ring buffer. The underlying payload `T` is strictly denied access to the Compute core until the temporal sequence is mathematically restored.