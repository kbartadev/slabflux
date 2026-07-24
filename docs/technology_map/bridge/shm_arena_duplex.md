# SlabFlux Bridge: SHM Arena Duplex (`shm_arena_duplex.hpp`)

## 1. Architectural Overview
Mapping complex, deeply linked C++ data structures (like ASTs, fragmented strings, or trees) across process boundaries via Shared Memory is notoriously difficult due to Linux Address Space Layout Randomization (ASLR). 
The `shm_arena_duplex` is a bidirectional shared memory matrix wrapper that completely neutralizes ASLR mismatches without imposing heavy serialization overhead.

## 2. Relative Offset Translation
When Process A maps `/dev/shm/matrix`, it might be placed at virtual address `0x7000`. When Process B maps the identical file, it might be placed at `0x9000`. If Process A writes a raw pointer (`0x7050`) into the shared memory, Process B will inevitably segfault when trying to read it.

The Arena Duplex intercepts absolute pointers at the boundary:
- Before transmission into the SHM boundary, the arena computes the distance between the absolute pointer and the base address of its local SHM mapping.
- It strips the absolute `T*` memory address down to a raw 32-bit integer offset.
- Only this relative offset is written into the shared memory ring.

## 3. Deserialization Projection
Upon receiving the payload:
- Process B reads the 32-bit integer offset.
- It reconstructs a valid, local absolute pointer by simply adding the offset to its own local SHM mapping's base address.

This achieves O(1) zero-cost serialization/deserialization. It mathematically guarantees memory safety between disparate processes mapping the identical SHM file at completely different virtual address horizons.

## 4. Integration with `offset_ptr`
The system integrates tightly with the `slabflux::core::offset_ptr<T>` smart pointer. This wrapper acts syntactically identical to standard pointers (`*`, `->`), but automatically handles the relative base-offset calculation under the hood, making cross-process data sharing transparent to the developer.
