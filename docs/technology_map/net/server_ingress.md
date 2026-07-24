# SlabFlux Net: Server Ingress (`slabflux/net/server_ingress.hpp`)

## 1. Architectural Justification
The `server_ingress` node is the primary orchestrator that connects the physical reception engines (`af_xdp_ingress` or `io_uring_ingress`) to the topological execution mesh. It serves as the absolute origin point for the `sovereign_signal` lifecycle.

## 2. Boundary Transitions
- **Kernel-Bypass Extraction**: It commands the underlying hardware ring drivers to harvest physical network frames. 
- **Envelope Forging**: Once a raw payload pointer is identified, `server_ingress` wraps it in a causally-aware memory envelope, embedding sequence horizons and injecting initial hardware timestamps (`__rdtsc()`).
- **Injection**: It pushes the initialized signal onto the inbound `network_conduit`, effectively shifting the data's domain from generic network memory into the rigorously typed application state machine.

## 3. Deterministic Sequencing
This module enforces strict ingress ordering. By acting as the sole entry gate for all market and control-plane data, it ensures that identical execution trajectories can be perfectly replicated during failover recovery simulations.