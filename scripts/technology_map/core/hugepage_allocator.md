# SlabFlux Core: HugePage Allocator (`hugepage_allocator.hpp`)

## 1. Architectural Overview
In memory-intensive data structures (like Order Books or distributed state matrices), navigating gigabytes of RAM exposes the CPU to Translation Lookaside Buffer (TLB) misses. A standard 4KB memory page requires the CPU to constantly halt and walk the page tables to translate Virtual Addresses to Physical Addresses.

The `hugepage_allocator` entirely eliminates TLB thrashing by natively interfacing with Linux `hugetlbfs`, utilizing 2MB or 1GB massive page structures.

## 2. Direct OS Bypass Allocation
The allocator circumvents standard libc heaps:
- It issues a direct `mmap` syscall initialized with `MAP_HUGETLB | MAP_ANONYMOUS | MAP_PRIVATE`.
- It enforces immediate physical residency by appending `MAP_POPULATE` and `MAP_LOCKED`, forcing the kernel to fault-in and wire the physical memory pages before the trading engine even boots.

## 3. TLB Warmup & Pre-Faulting Integration
Even with HugePages, initial memory access can trigger micro-stalls. 
The allocator integrates seamlessly with the `tlb_warmup` subsystem:
- Before the deterministic execution loop ignites, background threads execute aggressive `memset` operations across the entirety of the HugePage slabs.
- This "pre-faults" the memory, permanently seating the translation coordinates into the hardware TLB registers.

## 4. Zero-Copy Hardware DMA
Because the memory mapped by the `hugepage_allocator` is physically contiguous and locked, it is the primary target for kernel-bypass I/O architectures.
- **AF_XDP UMEM Integration**: The allocated slabs are passed directly into the AF_XDP socket registries. The Network Interface Card (NIC) PCIe DMA controllers are instructed to blast incoming Ethernet frames directly into these HugePages, achieving absolute zero-copy network reception.
- **io_uring Fixed Buffers**: Used for asynchronous NVMe journaling (`durable_journal`), avoiding intermediate kernel page-cache bouncing.

## 5. Rank-Aware Distribution
To maximize DRAM bandwidth, the allocator interfaces with the `rank_aware_allocator` wrappers. It intelligently offsets initial block pointers so that concurrent parallel threads do not aggressively contend for the same physical DRAM bank or channel, maximizing simultaneous memory bus utilization.