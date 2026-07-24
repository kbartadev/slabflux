# Foundation: SHM Arena Duplex (`slabflux/bridge/shm_arena_duplex.hpp`)

## 1. Architectural Justification
When sharing complex graph data or ASTs across the `/dev/shm` boundary between two Linux processes, Address Space Layout Randomization (ASLR) guarantees that absolute pointers will fail, leading to segmentation faults. The Arena Duplex provides mathematical pointer relativization, neutralizing ASLR without serialization loops.

## 2. Hardware Implementation Directives
- **Pointer Relativization**: Converts 64-bit absolute memory addresses to 32-bit offset vectors relative to the base mapping address of the local process.
- **O(1) Deserialization**: The receiving process re-assembles the absolute pointer locally via a single hardware ADD instruction (`local_base + offset`).
- **Offset Alignment**: Guarantees that internal SHM offset nodes adhere strictly to `alignas(64)`, preventing split-cache-line penalties across the QPI fabric between isolated processes.

## 3. Bibliography & Proofs
1. **Drepper, U.** (2007). *What Every Programmer Should Know About Memory*. Red Hat, Inc. (Section on Shared Memory and Virtual Memory architectures).
2. **Linux Kernel Organization**. *Documentation/vm/hugetlbpage.txt*. (Shared Memory mapping algorithms and boundary constraints).
3. **Kemerlis, R. B., et al.** (2014). *ret2dir: Rethinking Kernel Isolation*. USENIX Security Symposium. (For ASLR mechanics and associated mathematical translation mitigations).