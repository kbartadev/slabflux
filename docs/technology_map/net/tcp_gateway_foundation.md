# Foundation: TCP Routing Gateway Demultiplexer

## 1. Architectural Justification
When a raw Ethernet frame arrives, the system must demultiplex the 4-tuple (Source IP, Source Port, Dest IP, Dest Port) to a specific socket. Operating systems solve this using Hash Tables with linked-list collision chains or Red-Black Trees (e.g., Linux's RCU-protected socket tables). These structures require pointer chasing, guaranteeing severe cache misses (L2/L3) for every arriving packet.

The `tcp_gateway` abandons dynamic lookup trees. It routes physical frames to connections using a **Spatial Hash Matrix**, bounding lookup latency to absolute $O(1)$ execution.

## 2. Hardware Implementation Directives
- **Branchless Spatial Hashing:** The connection lookup (`conn_id & 1023`) uses a bitwise logical AND over a power-of-two array capacity. This mathematically maps the network frame to the pre-allocated TCB in a single CPU instruction, eliminating integer division (`DIV`) and loop traversals.
- **The Injection Triad:** The gateway acts as a strict memory bridge. It executes the sequence validation (Flow Engine), slices the 64-byte L2/L3/L4 header block off, and injects the remaining pointer directly into the `tcp_stream_defragmenter` sequentially, meaning the payload bytes are never copied into an intermediate routing buffer.

## 3. Bibliography & Proofs
1. **Rizzo, L.** (2012). *netmap: a novel framework for fast packet I/O*. USENIX ATC. (Demonstrates that the primary bottleneck in OS networking is the per-packet metadata allocation and routing lookup, not the copy itself).
2. **Belay, A., et al.** (2014). *IX: A Protected Dataplane Operating System for High Throughput and Low Latency*. OSDI. (Proofs on zero-copy demultiplexing directly from NIC queues to user-space connection tracking arrays).