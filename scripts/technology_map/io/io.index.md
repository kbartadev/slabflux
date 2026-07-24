# SLABFLUX IO Technology Map: Index

## 1. Summary Overviews
- [Architectural Blueprint](./io.blueprint.md)
- [Foundational References](./io.foundation.md)

## 2. Networking Engines (Kernel Bypass)
### AF_XDP
- [Blueprint](./io.af_xdp_ingress.blueprint.md) | [Foundation](./io.af_xdp_ingress.foundation.md)
### io_uring
- [Duplex Blueprint](./io.uring_duplex.blueprint.md) | [Duplex Foundation](./io.uring_duplex.foundation.md)
- [Ingress Blueprint](./io.uring_ingress.blueprint.md) | [Ingress Foundation](./io.uring_ingress.foundation.md)
- [Egress Blueprint](./io.uring_egress.blueprint.md) | [Egress Foundation](./io.uring_egress.foundation.md)
- [XDP Blueprint](./io.uring_ingress_xdp.blueprint.md) | [XDP Foundation](./io.uring_ingress_xdp.foundation.md)
- [Stream Blueprint](./io.uring_stream.blueprint.md) | [Stream Foundation](./io.uring_stream.foundation.md)
- [Baremetal Egress Blueprint](./io.baremetal_egress.blueprint.md) | [Baremetal Egress Foundation](./io.baremetal_egress.foundation.md)
### Windows RIO
- [Duplex Blueprint](./io.rio_duplex.blueprint.md) | [Duplex Foundation](./io.rio_duplex.foundation.md)
### DPDK
- [General Blueprint](./io.dpdk.blueprint.md) | [General Foundation](./io.dpdk.foundation.md)
- [Ingress Blueprint](./io.dpdk_ingress.blueprint.md) | [Ingress Foundation](./io.dpdk_ingress.foundation.md)
- [Egress Blueprint](./io.dpdk_egress.blueprint.md) | [Egress Foundation](./io.dpdk_egress.foundation.md)

## 3. Protocol Parsing
### Structural (Binary Protocols)
- [eader_parser Blueprint](./io.eader_parser.blueprint.md) | [eader_parser Foundation](./io.eader_parser.foundation.md)
- [Structural Parser Blueprint](./io.structural_parser.blueprint.md) | [Structural Parser Foundation](./io.structural_parser.foundation.md)
### Vectorized (Text Protocols)
- [Header Parser (SIMD) Blueprint](./io.header_parser.blueprint.md) | [Header Parser Foundation](./io.header_parser.foundation.md)
- [SIMD Parser Blueprint](./io.simd_parser.blueprint.md) | [SIMD Parser Foundation](./io.simd_parser.foundation.md)

## 4. Inter-Process Communication (SHM Nexus)
- [General Blueprint](./io.shm.blueprint.md) | [General Foundation](./io.shm.foundation.md)
- [Bridge Blueprint](./io.shm_bridge.blueprint.md) | [Bridge Foundation](./io.shm_bridge.foundation.md)
- [Duplex Blueprint](./io.shm_duplex.blueprint.md) | [Duplex Foundation](./io.shm_duplex.foundation.md)
- [Ingress Blueprint](./io.shm_ingress.blueprint.md) | [Ingress Foundation](./io.shm_ingress.foundation.md)
- [Egress Blueprint](./io.shm_egress.blueprint.md) | [Egress Foundation](./io.shm_egress.foundation.md)

## 5. Persistence & Hardware Instrumentation
### Journaling
- [Durable Journal Blueprint](./io.durable_journal.blueprint.md) | [Durable Journal Foundation](./io.durable_journal.foundation.md)
- [Mirrored Journal Blueprint](./io.mirrored_journal.blueprint.md) | [Mirrored Journal Foundation](./io.mirrored_journal.foundation.md)
### Hardware Control
- [Auxiliary Tools Blueprint](./io.hardware_aux.blueprint.md) | [Auxiliary Tools Foundation](./io.hardware_aux.foundation.md)
- [Shaper Blueprint](./io.hardware_shaper.blueprint.md) | [Shaper Foundation](./io.hardware_shaper.foundation.md)
- [Latency Monitor Blueprint](./io.wire_latency_monitor.blueprint.md) | [Latency Monitor Foundation](./io.wire_latency_monitor.foundation.md)

## 6. Design Strategies & Core Primitives
- [Strategy Blueprint](./io.strategy.blueprint.md) | [Strategy Foundation](./io.strategy.foundation.md)
- [Egress Blueprint](./io.egress.blueprint.md) | [Egress Foundation](./io.egress.foundation.md)
- [Stack Blueprint](./io.stack.blueprint.md) | [Stack Foundation](./io.stack.foundation.md)
