# SlabFlux Core: Topology (`topology.hpp`)

## 1. Architectural Overview
The general topology domain manages the high-level mapping of physical resources to software constructs. It sits above hardware-specific pinning and provides a declarative interface to allocate pipeline stages across the system.

## 2. Topology Traits
The topology system resolves abstract node roles (e.g., Ingress, Egress, Compute) to absolute NUMA nodes, Core IDs, and PCIe DMA channels.

## 3. Pre-Flight Enforcement
Before the `ignition_manifest` seals the application, the topology engine validates the hardware to ensure the physical motherboard matches the expected runtime constraints, halting the application if resources (like isolated cores) are misconfigured.