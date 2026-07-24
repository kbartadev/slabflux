# SlabFlux Core: Pinned Allocators (`pinned_allocator_spsc.hpp`, `pinned_allocator_mpmc.hpp`)

## 1. Architectural Overview
Standard C++ memory allocation (`malloc`/`new`) is non-deterministic, frequently invoking OS-level locks (`mmap`/`brk` syscalls) and yielding the CPU thread to the kernel scheduler. 
The SlabFlux `pinned_allocator` series provides explicit, deterministic, O(1) memory lifecycle management entirely within user-space, bypassing the OS scheduler completely.

## 2. Memory Sovereignty and Pinning
During the ignition phase, the allocator claims a vast contiguous block of physical RAM.
- **Physical Pinning (`mlock`)**: The allocator permanently locks the memory pages into physical RAM. This guarantees that the operating system's virtual memory manager will never swap these pages out to the disk subsystem, eradicating page-fault latency spikes on the hot path.
- **NUMA Affinity**: The allocations are strictly bound to the local NUMA node of the executing thread using `mbind()`, ensuring that memory access never traverses the high-latency QPI/Infinity Fabric interconnects.

## 3. Concurrency Variants
The allocator is mathematically specialized into two variants to match the exact concurrency requirements of the deployment topology:

### `pinned_allocator_spsc` (Wait-Free)
Designed for localized, Thread-Local Storage (TLS) or strict Single-Producer Single-Consumer pipelines.
- **Mechanism**: Utilizes localized free-lists without any atomic instructions or memory barriers. 
- **Performance**: Allocation and deallocation resolve in a handful of CPU cycles, bounded strictly by L1 cache speed.

### `pinned_allocator_mpmc` (Lock-Free)
Designed for highly contended, many-to-many execution matrices where multiple threads claim and release objects simultaneously.
- **Mechanism**: Built on an epoch-tagged lock-free stack. It uses 64-bit tagged Compare-And-Swap (CAS) operations to prevent the ABA problem during concurrent node recycling.
- **Performance**: Heavily amortizes CAS overhead via vectorized `allocate_batch` and `deallocate_batch` operations.

## 4. Cache-Line Geometry
Every allocated block is padded and aligned to `std::hardware_constructive_interference_size` (typically 64 bytes). This architectural guarantee ensures that objects allocated for Thread A never share a physical cache line with objects allocated for Thread B, physically preventing False Sharing degradation.