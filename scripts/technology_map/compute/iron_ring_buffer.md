# SlabFlux Conduit: Iron Ring Buffer (`iron_ring_buffer.hpp`)

## 1. Architectural Overview
The `iron_ring_buffer` is an uncompromising, statically-sized queue tailored for environments where queue wrap-around overhead and L1 cache misses must be mathematically eradicated. It acts as an enhanced, specialized variant of the `spsc_conduit`.

## 2. Power-of-Two Masking
Standard ring buffers execute integer division (modulo operator `%`) to wrap the head and tail cursors back to zero when they reach the end of the array. The `DIV` instruction is incredibly slow on modern x86-64 processors (often 10-20 cycles).

The Iron Ring Buffer bypasses this:
- It strictly forces the user to allocate a capacity that is a power of two (e.g., 1024, 2048) via a `static_assert(std::has_single_bit(Capacity))`.
- Wrapping is achieved using a single-cycle bitwise AND operation (`cursor & (Capacity - 1)`).
- This guarantees that sequence incrementation and boundary wrapping execute optimally.

## 3. Asynchronous Software Prefetching
The Iron Ring employs look-ahead hardware hinting:
- During the `try_pop` sequence, the buffer utilizes `_mm_prefetch(..., _MM_HINT_T0)` to signal the memory controller to fetch the *next* logical item in the queue.
- Because the CPU is warned in advance, the payload is fully staged in the L1 Data Cache by the time the next loop iteration requests it, effectively hiding DRAM latency entirely.

## 4. Sovereign Memory Alignment
The backing array of the `iron_ring_buffer` is wrapped in `alignas(64)` to align with physical cache boundaries. Furthermore, its memory is typically acquired from a pinned `hugepage_allocator`, guaranteeing the ring never experiences OS page faults during high-frequency bursts.