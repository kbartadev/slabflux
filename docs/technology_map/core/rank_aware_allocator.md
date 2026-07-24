# SlabFlux Core: Rank-Aware Allocator (`rank_aware_allocator.hpp`)

## 1. Architectural Overview
Standard allocators pull memory sequentially, which can cause multiple CPU cores to hammer the same physical DRAM rank or memory channel. The `rank_aware_allocator` distributes memory allocations intelligently across hardware memory channels to maximize concurrent memory bus utilization.

## 2. Bank Interleaving
By understanding the physical layout of the DDR4/DDR5 DIMMs, the allocator strides memory blocks using hardware-specific offsets. This prevents "bank conflicts" where parallel threads stall waiting for the same physical memory bank to precharge.

## 3. TLB / HugePage Synergy
Used directly in conjunction with the `hugepage_allocator`, ensuring that when large contiguous virtual slabs are requested, the physical pages underlying them are properly dispersed across all memory controllers on the motherboard.