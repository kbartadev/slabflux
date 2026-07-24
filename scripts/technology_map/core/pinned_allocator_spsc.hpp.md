# Blueprint: pinned_allocator_spsc.hpp

## Architectural Overview
A highly specialized, NUMA-aware allocator utilizing a High-Watermark (Shadow Pointer) pattern to amortize atomic updates across thread boundaries.

## Core Logic & Mechanisms
- **Shadow Pointer Amortization**: Caches the opposing thread's head/tail positions locally. Only queries the shared `std::atomic` variable via `memory_order_acquire` when the local shadow bounds are exhausted, completely silencing MESI protocol chatter.
- **Batch Releasing**: Exposes `release_batch` which resets multiple sequential sequence bounds in a single clock cycle.
- **NUMA Pinning**: Interrogates Linux `mbind` policies upon initialization to lock the memory block into the RAM bank physically closest to the specified execution core.