# Foundation: HugePage Allocator (`slabflux/core/hugepage_allocator.hpp`)

## 1. Architectural Justification
In memory-intensive data structures (like Order Books or distributed state matrices), navigating gigabytes of RAM exposes the CPU to Translation Lookaside Buffer (TLB) misses. A standard 4KB memory page requires the CPU to constantly halt and walk the page tables to translate Virtual Addresses to Physical Addresses.
The `hugepage_allocator` entirely eliminates TLB thrashing by natively interfacing with Linux `hugetlbfs`, utilizing 2MB or 1GB massive page structures.

## 2. Hardware Implementation Directives
- **Direct OS Bypass Allocation**: Issues `mmap(MAP_HUGETLB | MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE | MAP_LOCKED)`. This forces the kernel to wire the physical memory pages before the deterministic engine boots.
- **Zero-Copy Hardware DMA**: Because the mapped memory is physically contiguous and locked, it is directly registered to AF_XDP socket registries (`UMEM`) and `io_uring` fixed buffers, achieving absolute zero-copy network and disk I/O.
- **Rank-Aware Interleaving**: Intelligently offsets initial block pointers so concurrent parallel threads do not aggressively contend for the same physical DRAM bank or channel.

## 3. Bibliography & Proofs
1. **Drepper, U.** (2007). *What Every Programmer Should Know About Memory*. Red Hat, Inc. (Section on TLB structures and HugePage efficiencies).
2. **Gorman, M.** (2004). *Understanding the Linux Virtual Memory Manager*. Prentice Hall. (Chapter 9: Page Frame Reclamation and mlock constraints).
3. **Navarro, J., et al.** (2002). *Practical, transparent operating system support for superpages*. USENIX Symposium on Operating Systems Design and Implementation (OSDI).