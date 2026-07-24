# Blueprint: System & Hardware Interaction

## Architectural Overview
System integration within SlabFlux is inherently antagonistic to the operating system kernel. The framework seizes explicit control over physical CPU resources, actively neutralizing scheduler interference and OS jitter.

## Core Components
- **Cache Sovereignty Enforcers (`cache_partitioner.sh`)**: Interrogates MSRs (Model-Specific Registers) to enforce Intel Cache Allocation Technology (CAT), locking out background OS tasks from the application’s L3 cache slices.
- **Hardware Thread Pinning (`shield_cores.sh`, `hardware_topology.hpp`)**: Employs `sched_setaffinity` and Cgroup boundaries to completely isolate logic cores, ensuring zero context-switches and unimpeded execution of infinite polling loops.
- **Sub-Nanosecond Telemetry (`chip_telemetry.hpp`)**: Bypasses the OS Time API (`clock_gettime`) entirely. Instead, interrogates the CPU Time Stamp Counter via `__rdtsc()` and core frequencies via `APERF/MPERF` registers to emit raw physical latency profiling.
- **Physical Memory Anchors (`hugepage_allocator`)**: Reclaims Linux 2MB hugepages and permanently locks them into RAM via `mlock()`, eradicating page faults and TLB misses completely.