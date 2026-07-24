# SlabFlux Bridge: Authoritative Bridge (`authoritative_bridge.hpp`)

## 1. Architectural Overview
In a clustered High-Availability (HA) environment, the `authoritative_bridge` serves as the absolute arbiter of "truth." It is the boundary layer where non-deterministic external events (e.g., UDP multicast market data) are stamped with an irreversible Logical Sequence Number (LSN) and serialized into the deterministic causal mesh.

## 2. Zero-Stall Serialization
The bridge is designed to never block the hot-path network ingress, even during heavy NVMe disk writes.

### Journal Decoupling
When an external payload arrives, the bridge performs two instantaneous actions:
1. It allocates a `wire_frame_lsn` envelope, assigning it the next strictly monotonic `LSN` and the local `cluster_id`.
2. It reserves a slot in the `durable_journal` and asynchronously commits the frame without waiting for physical disk flush (`fsync`).

The actual propagation of the event into the deterministic `branchless_engine` occurs concurrently with the journaling process, ensuring microsecond execution latencies regardless of storage queue depth.

## 3. Gap-Fill Replay Mechanics
If a secondary (passive) node loses network connectivity or crashes, it will fall behind the authoritative LSN horizon. Upon reconnection, the bridge orchestrates a flawless, bit-perfect recovery.

### Watermark Verification
The bridge interrogates the recovering node's `sf_node_ctx` to identify the last successfully committed LSN (the watermark).

### Selective Journal Replay
Instead of requesting a full state snapshot (which saturates network bandwidth), the bridge streams only the missing `wire_frame_lsn` deltas directly from the local `durable_journal`. 
- The replay strictly enforces monotonicity. It iterates through the historical logs and actively drops any frame where `frame.lsn <= watermark`, mathematically guaranteeing that no event is ever applied twice.

## 4. Replicator Integration
While the primary engine digests the event locally, the bridge simultaneously hands the serialized `wire_frame_lsn` to the `network_replicator`. 
The replicator broadcasts the newly sequenced event to all connected passive readers, maintaining exact replica synchronization across the distributed state matrix.