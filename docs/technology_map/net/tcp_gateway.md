# SlabFlux Net: TCP Routing Gateway (`tcp_gateway.hpp`)

## 1. Architectural Overview
The `tcp_gateway` is the deterministic demultiplexer (demux) that physically bridges raw L2/L3 hardware acquisition (`matrix_nexus` or AF_XDP queues) to the internal L4 Virtual Sockets. It is the critical intercept point that evaluates physical network frames before they reach application memory.

## 2. Spatial Hash Demultiplexing
When a packet arrives from the wire, the gateway must associate it with the correct `tcp_transmission_control_block` (TCB). Traditional kernels use complex red-black trees or chained hash tables to look up sockets by their 4-tuple (SrcIP, SrcPort, DstIP, DstPort).

SlabFlux mandates an $O(1)$ constraint:
```cpp
tcp_transmission_control_block& tcb = tcbs_[conn_id & 1023];
```
The gateway uses a statically allocated array of TCBs and resolves the connection via a branchless bitmask spatial hash (`& 1023`). This entirely prevents cache misses associated with pointer-chasing during socket lookups.

## 3. The Injection Triad
The Gateway performs a highly orchestrated 3-step maneuver upon receiving a raw `raw_tcp_ipv4_frame`:

1.  **L4 Engine Evaluation:** The raw frame is passed to `tcp_flow_engine::process_inbound()`. This branchlessly validates the sequence bounds, updates the acknowledgment cursors, and processes protocol flags (SYN/FIN).
2.  **Payload Slicing:** If the flow engine returns `true` (packet accepted), the gateway mathematically slices the L2/L3/L4 header block off the front of the frame.
3.  **Defragmenter Injection:** The remaining payload (if any) is wrapped in an ephemeral `inbound_frame` struct and injected directly into the existing `tcp_stream_defragmenter`.

## 4. Synergy with Existing Architecture
Because the `tcp_gateway` natively formats the accepted payload into an `inbound_frame` and calls `defragmenter_.on()`, it perfectly integrates with the existing SlabFlux Layer 7 parsing stack. 

The `tcp_stream_defragmenter` reorders any out-of-sequence packets natively and streams the contiguous data into the `baremetal_parser` or `http_avx_parser`, completely decoupling the application logic from the chaos of raw TCP/IP packet transmission.

## 5. Security Invariants
The `on_raw_frame` hot-path is guarded by an immediate `length < sizeof(raw_tcp_ipv4_frame)` check. Malformed, undersized packets from port-scanners or DoS tools are discarded at the extreme physical edge of the framework before they can consume CPU cycles in the Flow Engine or Defragmenter.