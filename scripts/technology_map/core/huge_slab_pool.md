# SlabFlux Core: Huge Slab Pool (`huge_slab_pool.hpp`)

## 1. Architectural Overview
The `huge_slab_pool` merges the TLB-miss eradication of the `hugepage_allocator` with the O(1) fragmentation control of the `slab_allocator`. It is the definitive memory backend for allocating massive, homogenous entities (like entire Order Books or AI weight matrices) dynamically without trapping into the OS.

## 2. 1GB Page Backing
Unlike standard memory pools which allocate incrementally, the `huge_slab_pool` maps multi-gigabyte blocks of memory upon ignition using `MAP_HUGETLB | MAP_HUGE_1GB`.
- By forcing the kernel to use 1GB physical pages, a 64GB trading state matrix requires only 64 entries in the CPU's Translation Lookaside Buffer (TLB), compared to 16.7 million entries for standard 4KB pages.
- This virtually guarantees 100% TLB cache-hit rates, allowing the `vector_lane_engine` to traverse the entire memory space linearly without ever triggering a hardware page walk.

## 3. Wait-Free Slot Allocation
The pool divides the 1GB HugePages into strictly typed, cache-aligned slots (`T[N]`).
- A localized, lock-free LIFO stack tracks the available slots.
- Because the entire pool is backed by contiguous physical RAM, calculating pointers and bounds-checking compiles down to raw arithmetic bit-shifts, delivering allocations in under 5 nanoseconds.