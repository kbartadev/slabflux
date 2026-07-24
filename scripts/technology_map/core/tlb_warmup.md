# SlabFlux Core: TLB Warmup (`tlb_warmup.hpp`)

## 1. Architectural Overview
In latency-critical environments, simply allocating memory using `mmap` (even with `MAP_POPULATE` and `MAP_LOCKED`) is not enough to guarantee zero jitter. The CPU's Translation Lookaside Buffer (TLB) caches the mapping from Virtual Addresses to Physical Addresses. If a hot-path thread touches an unmapped page, the CPU stalls for hundreds of cycles performing a hardware "Page Walk".

The `tlb_warmup` module systematically pre-faults and permanently seats these mappings into the hardware TLB before live trading or inference begins.

## 2. Aggressive Pre-Faulting
During the `ignition_manifest` phase, the `tlb_warmup` utility executes a deliberate sweep across all pre-allocated memory slabs (such as those owned by the `hugepage_allocator` or `mpmc_pool`).

- **Cache-Line Striding**: It sequentially writes a zero byte into every single 64-byte cache line across the entire allocated memory block.
- **Hardware Forcing**: This forced write operation compels the memory controller and the OS to definitively wire the physical memory page, mathematically guaranteeing that the OS Page Fault handler is never invoked during the operational lifecycle.

## 3. HugePage Synergy
While sweeping standard 4KB pages requires thousands of TLB entries (which quickly saturate and evict each other), sweeping 2MB or 1GB HugePages requires significantly fewer TLB registers.
- The `tlb_warmup` utility detects the underlying page geometry.
- When combined with HugePages, the entire trading state (Order Books, Conduits, AI matrices) fits entirely within the L1/L2 TLB caches, granting the execution engine 100% O(1) physical memory resolution with zero TLB misses.

## 4. Execution Timing
Because warming up gigabytes of RAM causes massive memory bus saturation, this utility is strictly restricted to the startup sequence. Once the `tlb_warmup` completes, it issues a `std::atomic_thread_fence(std::memory_order_seq_cst)` to clear the memory pipelines before yielding to the live `branchless_engine`.