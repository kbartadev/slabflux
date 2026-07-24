# SlabFlux Core: Offset Pointers (`offset_ptr.hpp`)
# Offset Pointer (`offset_ptr.hpp`)

## 1. Architectural Overview
In multi-process architectures leveraging Inter-Process Communication (IPC) via shared memory (e.g., `/dev/shm`), raw C++ pointers (`T*`) are lethally dangerous. Because of Address Space Layout Randomization (ASLR), a shared memory segment will be mapped to entirely different virtual addresses in Process A and Process B.

The `offset_ptr<T>` provides a zero-overhead, strictly safe C++ smart pointer designed explicitly to construct deeply linked data structures (like lists, trees, or fragmented strings) inside IPC shared memory arrays.

## 2. Relative Memory Geometry
Instead of storing a 64-bit absolute virtual address, the `offset_ptr` stores a 32-bit (or 64-bit) relative signed integer offset.
- **Serialization**: When an object is assigned to an `offset_ptr`, the system calculates the geometric distance between the physical memory location of the pointer itself and the target object (`target_address - this_address`).
- **Deserialization**: When the pointer is dereferenced (`operator*` or `operator->`), the system adds the internal offset back to its own `this` pointer (`this_address + offset`).

Because the *relative distance* between two objects residing in the same shared memory slab is mathematically identical regardless of where the slab is mapped in virtual memory, the pointer resolves flawlessly in both Process A and Process B.

## 3. Ergonomic Integration
Despite operating on relative offsets, `offset_ptr<T>` perfectly mimics the interface of standard raw pointers:
- Supports `nullptr` equivalents (typically represented by an offset of `1` or `0`, depending on alignment masks).
- Provides `get()`, `operator->`, and `operator*`.
- Satisfies C++ iterator concepts, allowing `std::find` or `std::sort` to traverse IPC-linked structures natively.

## 4. Structural Alignment Invariants
To maximize AVX-512 performance and prevent cache-line splitting during relative calculations, the implementation enforces strict alignments. Computations internally utilize compiler intrinsics to ensure the offset additions never invoke undefined behavior or trigger unaligned read penalties at the hardware level.