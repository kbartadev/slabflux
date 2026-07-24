# SLABFLUX IO Technology Map: Index
# SLABFLUX I/O Technology Map: Index

## 1. Summary Overviews
- [Architectural Blueprint](./blueprint_io.md)
- [Foundational References](./io.md)
## 1. Reception (Ingress)
- [AF_XDP Ingress Docs](./af_xdp_ingress.md) | [Blueprint](./blueprint_af_xdp_ingress.md) | [Foundation](./foundation_af_xdp_ingress.md)
- [io_uring Ingress Docs](./io_uring_ingress.md) | [Blueprint](./blueprint_io_uring_ingress.md) | [Foundation](./foundation_io_uring_ingress.md)
- [io_uring Ingress Stream Docs](./uring_ingress_stream.md) | [Blueprint](./blueprint_uring_ingress_stream.md) | [Foundation](./foundation_uring_ingress_stream.md)
- [DPDK Ingress Docs](./dpdk_ingress.md) | [Blueprint](./blueprint_dpdk_ingress.md) | [Foundation](./foundation_dpdk_ingress.md)
- [Socket Ingress Docs](./socket_ingress.md) | [Blueprint](./blueprint_socket_ingress.md) | [Foundation](./foundation_socket_ingress.md)
- [Uring Ingress Docs](./uring_ingress.md) | [Blueprint](./blueprint_uring_ingress.md) | [Foundation](./foundation_uring_ingress.md)

## 2. Networking Engines (Kernel Bypass)
### AF_XDP
- [AF_XDP Ingress Docs](./af_xdp_ingress.md) | [Blueprint](./blueprint_af_xdp_ingress.md) | [Foundation](./foundation_af_xdp_ingress.md)
### io_uring
- [Duplex Docs](./uring_duplex.md) | [Blueprint](./blueprint_uring_duplex.md) | [Foundation](./foundation_uring_duplex.md)
- [Ingress Docs](./io_uring_ingress.md) | [Blueprint](./blueprint_io_uring_ingress.md) | [Foundation](./foundation_io_uring_ingress.md)
- [Egress Docs](./uring_egress.md) | [Blueprint](./blueprint_uring_egress.md) | [Foundation](./foundation_uring_egress.md)
- [XDP Docs](./uring_ingress_xdp.md) | [Blueprint](./blueprint_uring_ingress_xdp.md) | [Foundation](./foundation_uring_ingress_xdp.md)
- [Stream Docs](./uring_stream.md) | [Blueprint](./blueprint_uring_stream.md) | [Foundation](./foundation_uring_stream.md)
## 2. Transmission (Egress)
- [DPDK Egress Docs](./dpdk_egress.md) | [Blueprint](./blueprint_dpdk_egress.md) | [Foundation](./foundation_dpdk_egress.md)
- [io_uring Egress Docs](./uring_egress.md) | [Blueprint](./blueprint_uring_egress.md) | [Foundation](./foundation_uring_egress.md)
- [io_uring Egress Stream Docs](./uring_egress_stream.md) | [Blueprint](./blueprint_uring_egress_stream.md) | [Foundation](./foundation_uring_egress_stream.md)
- [Baremetal Egress Docs](./baremetal_egress.md) | [Blueprint](./blueprint_baremetal_egress.md) | [Foundation](./foundation_baremetal_egress.md)
### Windows RIO
- [Duplex Docs](./rio_duplex.md) | [Blueprint](./blueprint_rio_duplex.md) | [Foundation](./foundation_rio_duplex.md)
### DPDK
- [General Docs](./dpdk.md) | [Blueprint](./blueprint_dpdk.md) | [Foundation](./foundation_dpdk.md)
- [Ingress Docs](./dpdk_ingress.md) | [Blueprint](./blueprint_dpdk_ingress.md) | [Foundation](./foundation_dpdk_ingress.md)
- [Egress Docs](./dpdk_egress.md) | [Blueprint](./blueprint_dpdk_egress.md) | [Foundation](./foundation_dpdk_egress.md)
- [Socket Egress Docs](./socket_egress.md) | [Blueprint](./blueprint_socket_egress.md) | [Foundation](./foundation_socket_egress.md)

## 3. Protocol Parsing
### Structural (Binary Protocols)
## 3. Bidirectional (Duplex) & Streams
- [io_uring Duplex Docs](./uring_duplex.md) | [Blueprint](./blueprint_uring_duplex.md) | [Foundation](./foundation_uring_duplex.md)
- [io_uring Duplex XDP Docs](./uring_duplex_xdp.md) | [Blueprint](./blueprint_uring_duplex_xdp.md) | [Foundation](./foundation_uring_duplex_xdp.md)
- [RIO Duplex Docs](./rio_duplex.md) | [Blueprint](./blueprint_rio_duplex.md) | [Foundation](./foundation_rio_duplex.md)
- [Socket Duplex Docs](./socket_duplex.md) | [Blueprint](./blueprint_socket_duplex.md) | [Foundation](./foundation_socket_duplex.md)
- [io_uring Stream Docs](./uring_stream.md) | [Blueprint](./blueprint_uring_stream.md) | [Foundation](./foundation_uring_stream.md)

## 4. Parsers & Protocol Extractors
- [SIMD Parser Docs](./simd_parser.md) | [Blueprint](./blueprint_simd_parser.md) | [Foundation](./foundation_simd_parser.md)
- [Header Parser Docs](./header_parser.md) | [Blueprint](./blueprint_header_parser.md) | [Foundation](./foundation_header_parser.md)
- [Structural Parser Docs](./structural_parser.md) | [Blueprint](./blueprint_structural_parser.md) | [Foundation](./foundation_structural_parser.md)
### Vectorized (Text Protocols)
- [Header Parser (SIMD) Docs](./header_parser.md) | [Blueprint](./blueprint_header_parser.md) | [Foundation](./foundation_header_parser.md)
- [SIMD Parser Docs](./simd_parser.md) | [Blueprint](./blueprint_simd_parser.md) | [Foundation](./foundation_simd_parser.md)
- [HTTP AVX Parser Docs](./http_avx.md) | [Blueprint](./blueprint_http_avx.md) | [Foundation](./foundation_http_avx.md)

## 4. Inter-Process Communication (SHM Nexus)
- [General Docs](./shm.md) | [Blueprint](./blueprint_shm.md) | [Foundation](./foundation_shm.md)
- [Bridge Docs](./shm_bridge.md) | [Blueprint](./blueprint_shm_bridge.md) | [Foundation](./foundation_shm_bridge.md)
- [Duplex Docs](./shm_duplex.md) | [Blueprint](./blueprint_shm_duplex.md) | [Foundation](./foundation_shm_duplex.md)
- [Ingress Docs](./shm_ingress.md) | [Blueprint](./blueprint_shm_ingress.md) | [Foundation](./foundation_shm_ingress.md)
- [Egress Docs](./shm_egress.md) | [Blueprint](./blueprint_shm_egress.md) | [Foundation](./foundation_shm_egress.md)

## 5. Persistence & Hardware Instrumentation
### Journaling
- [Durable Journal Docs](./io_uring_durable_journal.md) | [Blueprint](./blueprint_io_uring_durable_journal.md) | [Foundation](./foundation_io_uring_durable_journal.md)
## 5. Persistence & Hardware Core
- [io_uring Durable Journal Docs](./io_uring_durable_journal.md) | [Blueprint](./blueprint_io_uring_durable_journal.md) | [Foundation](./foundation_io_uring_durable_journal.md)
- [Mirrored Journal Docs](./mirrored_journal.md) | [Blueprint](./blueprint_mirrored_journal.md) | [Foundation](./foundation_mirrored_journal.md)
### Hardware Control
- [Auxiliary Tools Docs](./hardware_aux.md) | [Blueprint](./blueprint_hardware_aux.md) | [Foundation](./foundation_hardware_aux.md)
- [Shaper Docs](./hardware_shaper.md) | [Blueprint](./blueprint_hardware_shaper.md) | [Foundation](./foundation_hardware_shaper.md)
- [Latency Monitor Docs](./wire_latency_monitor.md) | [Blueprint](./blueprint_wire_latency_monitor.md) | [Foundation](./foundation_wire_latency_monitor.md)
- [Non-Temporal Writer Docs](./non_temporal_writer.md) | [Blueprint](./blueprint_non_temporal_writer.md) | [Foundation](./foundation_non_temporal_writer.md)
- [Hardware Aux Docs](./hardware_aux.md) | [Blueprint](./blueprint_hardware_aux.md) | [Foundation](./foundation_hardware_aux.md)
- [DPDK Core Docs](./dpdk.md) | [Blueprint](./blueprint_dpdk.md) | [Foundation](./foundation_dpdk.md)

## 6. Design Strategies & Core Primitives
- [Strategy Docs](./strategy.md) | [Blueprint](./blueprint_strategy.md) | [Foundation](./foundation_strategy.md)
- [Egress Docs](./egress.md) | [Blueprint](./blueprint_egress.md) | [Foundation](./foundation_egress.md)
- [Stack Docs](./stack.md) | [Blueprint](./blueprint_stack.md) | [Foundation](./foundation_stack.md)

## 7. Time & Hardware Streaming
- [Clock Node Docs](./clock_node.md) | [Blueprint](./blueprint_clock_node.md) | [Foundation](./foundation_clock_node.md)
- [Non-Temporal Writer Docs](./non_temporal_writer.md) | [Blueprint](./blueprint_non_temporal_writer.md) | [Foundation](./foundation_non_temporal_writer.md)
- [Buffer Flush Docs](./buffer_flush.md) | [Blueprint](./blueprint_buffer_flush.md) | [Foundation](./foundation_buffer_flush.md)
- [Endian Math Docs](./endian.md) | [Blueprint](./blueprint_endian.md) | [Foundation](./foundation_endian.md)
## 6. IPC & Shared Memory
- [SHM Nexus Docs](./shm.md) | [Blueprint](./blueprint_shm.md) | [Foundation](./foundation_shm.md)
- [SHM Bridge Docs](./shm_bridge.md) | [Blueprint](./blueprint_shm_bridge.md) | [Foundation](./foundation_shm_bridge.md)
