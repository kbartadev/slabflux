# SlabFlux Core: TLB Warmup & Pre-Faulting (`tlb_warmup.hpp`)

## 1. Architectural Overview
The TLB Warmup subsystem completely eliminates non-deterministic latency spikes caused by on-demand paging and Translation Lookaside Buffer (TLB) misses during hot-path execution. 

## 2. Aggressive Pre-Faulting
When the Linux kernel allocates memory (even HugePages), it typically does so lazily. 
- During engine ignition, the allocator forcibly writes zero-bytes across the entirety of the allocated memory slabs.
- This forced access triggers the minor page faults required to physically map the virtual addresses to hardware RAM *before* the application goes live.

## 3. Memory Locking (`mmap`)
- The `MAP_POPULATE` flag is utilized alongside `mmap` to instruct the kernel to immediately map physical pages.
- The `MAP_LOCKED` system flag (and subsequent `mlock()` calls) guarantees that the allocated pages are never swapped out to disk by the kernel's memory management subsystem.

## 4. TLB Cache Pinning
The Translation Lookaside Buffer (TLB) is a tiny, extremely fast hardware cache inside the CPU that maps virtual addresses to physical pages.
- By continuously accessing the pre-warmed memory boundaries in background threads during the boot sequence, the physical page addresses are forcefully locked into the CPU's hardware TLB cache.
- Once the network ingress goes live, the hot path experiences O(1) physical memory resolution with zero TLB-miss latency penalties.