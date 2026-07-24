# Blueprint: Rank-Aware Allocator

## Architectural Overview
Exploits the physical layout of modern DIMM modules, interleaving memory allocations across different hardware memory channels and ranks to maximize aggregate RAM bandwidth.

## Core Logic & Mechanisms
- **Channel Interleaving**: Distributes adjacent structures in the pool across multiple physical memory controllers rather than packing them linearly, effectively parallelizing the hardware's data bus capacity.
- **Bandwidth Saturation**: Directly combats the memory-wall bottleneck during massive AVX-512 vector sweeps, where processing speed eclipses the ability of a single RAM stick to feed the CPU execution units.