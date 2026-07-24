# Blueprint: Transport & Wire Protocols

## Architectural Overview
Transport modules encode and decode the deterministic bounds of raw byte-arrays moving between isolated subsystems. Framing mechanisms rely solely on raw offset calculation without dynamic header instantiation.

## Core Components
- **Raw Framing Protocols (`raw_tcp_frame`, `wire_protocol`)**: Mandates cache-aligned header topologies that directly embed into the payloads. Frames are accessed solely through `reinterpret_cast` across contiguous network buffers.
- **Deterministic Tokenization (`tagged_pointer`)**: Condenses type metadata and spatial location into a single 64-bit integer registry, transporting contextual identification through `spsc_conduit` structures across physical CPU threads.
- **Jitter-Tolerant Reassembly (`sequence_reorder`)**: Maintains an allocation-free rolling window that buffers out-of-order network frames, restoring absolute logical sequence determinism before allowing injection into the hot-path reactor.