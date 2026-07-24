# SlabFlux I/O: Network Stack (`slabflux/io/stack.hpp`)

## 1. Architectural Justification
Orchestrates the lifecycle and aggregation of multiple underlying I/O engines (ingress, egress, duplex) into a unified application-level network topology, bypassing the OS kernel's VFS layer.

## 2. Hardware Implementation Directives
- **Memory Arena Binding**: Pre-allocates and binds the massive shared `mempool` structures required by the constituent I/O modules during startup.
- **Thread Topology Layout**: Handles the NUMA-aware physical core assignments for the disparate polling threads (e.g., dedicating Core 2 to `af_xdp_ingress` and Core 4 to `uring_egress`).

## 3. Pipeline Integration
Serves as the bootstrap macro-component for the entire I/O domain. It initializes the physical networking boundary before the SPSC conduits and the `branchless_engine` are spun up, ensuring stable environmental preconditions.