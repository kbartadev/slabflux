# Blueprint: hardware_topology.hpp

## Architectural Overview
Dominates physical system constraints by explicitly assigning memory and processing matrices to distinct geographical coordinates on the motherboard.

## Core Logic & Mechanisms
- **Strict CPU Core Pinning**: Disables the OS scheduler's ability to migrate threads. Locks execution conduits permanently to targeted silicon via `sched_setaffinity`.
- **NUMA Memory Affinities**: Forces `mmap` backing structures into the RAM blocks locally associated with the executing NUMA node, completely neutralizing QPI latency bridges.