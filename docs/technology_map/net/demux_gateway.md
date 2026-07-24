# SlabFlux Net: Demux Gateway (`slabflux/net/demux_gateway.hpp`)

## 1. Architectural Justification
High-throughput systems receive varied message types (market data, order executions, control commands) over a single physical pipe. The `demux_gateway` isolates these streams and routes them to their designated algorithmic topological channels.

## 2. Hardware Implementation Directives
- **Spatial Dispatch**: Reads the pre-validated protocol Type ID (extracted by `header_parser`) and uses an array of tagged function pointers to route the message.
- **NUMA Local Handoff**: When spanning across threads, the gateway ensures that pointers are pushed to SPSC conduits residing within the same NUMA node to prevent QPI/UPI bus saturation.

## 3. Pipeline Integration
Functions as the main router within the I/O thread. It consumes the structurally validated payloads from the `bimodal_shield_wiring` and fans them out into specific concurrent `network_conduit` paths leading to parallel strategies.