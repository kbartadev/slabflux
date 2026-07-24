# SlabFlux Core: Pinned Allocator SPSC (`pinned_allocator_spsc.hpp`)

## 1. Architectural Overview
An ultra-fast, NUMA-aware, Single-Producer Single-Consumer memory pool designed for zero-allocation object recycling across isolated pipelines.

## 2. Dedicated Node Pinning
Uses OS-level strict bindings (`mbind`) to lock the allocator physically onto the same silicon die as the threads communicating over it, avoiding QPI/Infinity Fabric traversal latencies.

## 3. Wait-Free Reacquisition
Because it is locked to an SPSC paradigm, it avoids atomic CAS (Compare-And-Swap) loops entirely. Object allocation and deallocation execute linearly in under 5 CPU cycles, yielding the ultimate low-latency memory dispenser.