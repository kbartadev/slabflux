# SlabFlux Net: Network Architecture General Overview

## 1. Architectural Justification
The SlabFlux Network module (`slabflux/net/`) is the topological mesh bridging raw I/O boundaries with the deterministic compute algorithms. It establishes a sub-microsecond causal routing matrix, integrating wait-free conduits, zero-copy transport models, and autonomous boundary security.

## 2. Core Operational Constraints
- **Wait-Free Topologies**: All internal routing paths (`network_conduit`) operate on Single-Producer Single-Consumer (SPSC) rings. Mutexes, semaphores, and spinlocks are strictly prohibited.
- **Mathematical Shielding**: Untyped byte streams entering the domain are subjected to Symplectic Resonance Fencing and geometric validations (`bimodal_shield_wiring`, `demux_gateway`) before being mapped into typed C++ execution.
- **Zero Dynamic Allocation**: The entire lifecycle of an event—from ingress reception to egress transmission—reuses pre-pinned contiguous physical memory blocks, rendering `malloc` and `free` obsolete on the hot path.

## 3. Deterministic Flow
Inbound messages are transformed into `sovereign_signal` envelopes and stamped with temporal context. They pass through defragmenters and demultiplexers and enter the deterministic core. Outbound results follow the exact reverse pipeline through egress shields directly back to the physical wire.