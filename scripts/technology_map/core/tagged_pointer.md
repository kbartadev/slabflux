# Blueprint: tagged_pointer.hpp

## Architectural Overview
Creates a 64-bit transport token that fuses metadata directly with a memory address, replacing bloated dynamic headers typically attached to polymorphic objects.

## Core Logic & Mechanisms
- **Bit Packing**: Leverages the unused most-significant bits of standard x86-64 memory addresses to store a 16-bit type ID.
- **O(1) Resolution**: Allows `demuxer.hpp` to extract both the physical object pointer and its strict structural type definition in a single CPU register cycle before pushing it into the compute domains.