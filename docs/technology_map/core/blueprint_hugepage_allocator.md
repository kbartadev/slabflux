# Blueprint: hugepage_allocator.hpp

## Architectural Overview
Elimrates runtime Translation Lookaside Buffer (TLB) misses and OS page faults by forcing memory allocations into Linux 2MB or 1GB HugePages.

## Core Logic & Mechanisms
- **mmap HugeTLB Integration**: Maps arenas using `MAP_ANONYMOUS | MAP_PRIVATE | MAP_HUGETLB`.
- **Kernel Fault Bypassing**: Instantly locks the physical RAM via `MAP_POPULATE` and `MAP_LOCKED` flag constraints, forcing the kernel to wire the pages immediately instead of faulting upon first access.