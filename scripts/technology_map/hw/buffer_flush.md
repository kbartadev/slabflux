# Blueprint: buffer_flush.hpp

## Architectural Overview
Enforces explicit hardware ordering constraints when moving data directly out of the CPU's Line Fill Buffers (LFB) into physical PCIe lanes or RAM.

## Core Logic & Mechanisms
- **SFENCE Barriers**: Injects pure `_mm_sfence()` hardware fences immediately following non-temporal streaming writes to guarantee that outbound data becomes globally visible to peripheral devices (like a NIC or NVMe).
- **Compiler Reorder Halting**: Deploys `asm volatile("":::"memory")` barriers to lock compiler optimizations and prevent write-combining phases from interleaving logically sequential transmissions.