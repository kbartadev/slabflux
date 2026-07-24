# SlabFlux Sys: Cache Partitioner (`cache_partitioner.hpp`)

## 1. Architectural Overview
In modern multi-core processors, the Last Level Cache (L3 Cache) is shared across all cores. Background tasks, SSH sessions, and OS interrupts constantly fetch useless data into the L3 cache, evicting the critical memory blocks (like Order Books or Conduits) required by the trading engine.

The `cache_partitioner` establishes sovereign boundaries over the physical cache circuitry, mathematically guaranteeing that the SlabFlux hot path is never evicted by "noisy neighbors."

## 2. Intel Resource Director Technology (RDT)
The partitioner interfaces directly with the Intel CAT (Cache Allocation Technology) Model-Specific Registers (MSRs).

### Class of Service (CLOS) Separation
1. **The Sovereign Slice**: The partitioner configures a dedicated CLOS for the SlabFlux pinned cores, granting them exclusive read/write access to the majority of the L3 cache ways (e.g., 80%).
2. **The Isolation Slice**: It restricts all other CPU cores, kernel threads, and background daemon processes to a separate, non-overlapping CLOS (the remaining 20%).

This achieves absolute hardware-level physical isolation. No matter how much memory the OS allocates in the background, it physically cannot touch the silicon holding the trading state.

## 3. Runtime Enforcement
The configuration is enforced during the `ignition_manifest` boot sequence. If the CPU lacks CAT capabilities (identified via `isa_guard`), the system gracefully logs a warning and relies on memory affinity and thread-pinning to approximate the isolation.