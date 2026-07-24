# SlabFlux Core: Tagged Pointer (`tagged_pointer.hpp`)

## 1. Architectural Overview
Creates a 64-bit transport token fusing topological metadata directly with a hardware memory address, eliminating the need for bloated dynamic headers or polymorphic `vtable` wrappers on the hot path.

## 2. Hardware Bit Packing
Leverages the unused most-significant bits (top 16 bits) of standard x86-64 virtual memory addresses:
- Embeds an immutable Type ID and Route Tag directly into the pointer.
- Allows the `demux_gateway` to instantly identify and route the underlying payload without dereferencing memory.

## 3. Branchless Extraction
Restores the physical object pointer natively via a single bitwise `AND` mask in one CPU register cycle, pushing the extracted structure straight to the dispatcher.