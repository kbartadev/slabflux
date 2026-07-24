# Memory Model and Hardware Physics

## Atomic Memory Ordering

SLABFLUX uses Acquire-Release semantics to minimize CPU pipeline stalls.

- **Allocation:** `compare_exchange_weak` with `memory_order_release` on success and `memory_order_acquire` on failure.
- **Deallocation:** Writes to `next_index` occur before the atomic store to ensure global visibility.

## ABA Protection

The pool uses a 64-bit atomic state:
- `[63:32] Tag` - Inslabfluxmenting counter to prevent ABA issues.
- `[31:0] Index` - The free-list pointer.

## False Sharing

All atomic counters are padded using `std::hardware_destructive_interference_size` to ensure they reside on distinct L1 cache lines, preventing MESI protocol thrashing.
