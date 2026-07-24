# Blueprint: shm_arena_duplex.hpp

## Architectural Overview
A bidirectional SHM matrix wrapper that completely neutralizes Address Space Layout Randomization (ASLR) mismatches between independent processes.

## Core Logic & Mechanisms
- **Relative Offset Translation**: Automatically intercepts and strips absolute `T*` memory addresses to base-relative integer offsets before transmission into the SHM boundary.
- **Deserialization Projection**: Reconstructs pointers by adding the local mapping's base offset to the received integer, guaranteeing memory safety when processes map the same SHM file at completely different virtual address ranges.