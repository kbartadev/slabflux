# SlabFlux Core: Diffusion Conduits (`spatial_diffusion_conduit.hpp`, `ordered_diffusion_conduit.hpp`, `spsc_diffusion_conduit.hpp`)

## 1. Architectural Overview
Traditional lock-free queues (like `mpmc_conduit`) rely on centralized, monotonically increasing atomic counters (`head` and `tail`) to sequence producers and consumers. In extreme many-core environments, these shared counters become a severe bottleneck due to MESI cache-line bouncing (RFO stalls).

The **Diffusion Conduit** family represents a radical departure from traditional queueing theory: it completely abolishes centralized atomic sequence tickets in favor of a 1-byte detached state matrix evaluated natively by SIMD registers.

## 2. SIMD Wavefront Sweeping
Instead of performing an atomic `fetch_add` to claim a slot, threads act like radar arrays scanning a physical grid:
- The conduit maintains a compressed `uint8_t` state array (0 = Empty, 1 = Reserved, 2 = Ready, 3 = Claimed).
- A producer loads 32 state bytes into a single AVX2 `__m256i` register.
- It executes `_mm256_cmpeq_epi8` to find `STATE_EMPTY`, and extracts the available slots in a single CPU cycle using hardware bit-scanning (`_mm256_movemask_epi8` + `__builtin_ctz` or `countr_one`).

## 3. Structural Variants

### `spatial_diffusion_conduit` (Maximum Throughput)
- **Unordered Scatter**: Threads are assigned a unique, hash-based starting index (Cursor). They immediately drift apart and scatter across the matrix.
- **Zero RFO Contention**: Because no two threads start at the same location, they virtually never touch the same physical cache line, unlocking mathematically perfect linear scaling across NUMA nodes.

### `ordered_diffusion_conduit` (Strict FIFO)
- **Wavefront Clipping**: Maintains strict chronological ordering by advancing a lazy, shared cursor. 
- Threads use bitmask clipping (`mask &= ~((1ULL << bit_offset) - 1)`) to ensure they only claim slots *ahead* of the active wavefront.
- **Head-of-Line Bypass**: If Thread A stalls while writing to slot 0, Thread B can still successfully pop slot 1. This achieves true wait-free concurrency without strict blocking.

### `spsc_diffusion_conduit` (The Ultimate 1:1 Matrix)
- Drops all shared atomics entirely. The Producer and Consumer maintain purely local cursors.
- Uses AVX-512 `_mm512_storeu_si512` to dump up to 64 bytes of state updates instantaneously.

## 4. Hardware SMT Yielding
When the wavefront identifies zero available slots (`mask == 0`), the threads yield execution ports to their Hyper-Threading siblings via `_mm_pause()` rather than attempting aggressive, bus-locking CAS retries.