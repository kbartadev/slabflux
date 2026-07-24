# SlabFlux Compute: MDL State Array (`mdl_state_array.hpp`)

## 1. Architectural Overview
The `mdl_state_array` is the 5th Pillar of the Quintipartite Hardware Defense. It acts as a drop-in replacement for standard C-arrays or `std::array` inside deterministic `StateLogic` definitions.

## 2. Lorentz Subsumption
The array automatically fuses data-at-rest to the temporal LSN Light-Cone using the `minkowski_lattice`.
- When data is written (`write_sealed`), it is anchored to the sequence clock.
- When data is read (`read_subsumed` or `bulk_subsumption_read`), the hardware evaluates the Minkowski Interval.
- If the memory has rotted due to Rowhammer or cosmic rays, the hardware natively returns absolute zero without branching.

## 3. Hardware-Accelerated Bulk Sweeps
The array is physically `alignas(64)` and enforces capacities that are multiples of 16. This guarantees perfect AVX-512 loop unrolling when the Vector Lane Engine performs bulk state subsumption reads. By processing the arrays in massive chunks, the geometric integrity checks are completely hidden inside the L1 cache memory-fetch latency.