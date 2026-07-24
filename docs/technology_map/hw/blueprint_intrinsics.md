# Blueprint: Hardware Intrinsics

## Architectural Overview
Core C++ compiler intrinsic wrappers. Encapsulates `_mm_prefetch`, `_mm_cldemote`, and memory fences (`_mm_sfence`) to enforce manual hardware cache topologies around network data.