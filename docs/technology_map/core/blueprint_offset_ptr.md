# Blueprint: Offset Pointers

## Architectural Overview
Provides pointer alternatives that utilize relative byte offsets rather than absolute memory addresses, creating ASLR-agnostic state structures.

## Core Logic & Mechanisms
- **Base-Relative Resolution**: Calculates target memory locations internally by adding a `ptrdiff_t` offset to the object's `this` pointer or a designated arena base pointer.
- **Shared Memory Safety**: Guarantees perfect memory tracking when moving complex structures across `shm_bridge` boundaries, surviving identical objects being mapped at wildly differing virtual addresses in distinct OS processes.