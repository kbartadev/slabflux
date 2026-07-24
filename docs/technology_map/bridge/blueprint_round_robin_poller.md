# Blueprint: round_robin_poller.hpp

## Architectural Overview
Implements lock-free Fan-In logic. It aggregates multiple independent incoming single-producer networks (e.g., discrete NIC interfaces) into a centralized processing thread.

## Core Logic & Mechanisms
- **Mathematical Fairness**: Rotates its reading operations sequentially across bonded `spsc_conduit` structures using unrolled branchless indices.
- **Starvation Prevention**: Guarantees that high-velocity networks cannot drown out lower-volume interfaces by limiting burst reads per conduit cycle.