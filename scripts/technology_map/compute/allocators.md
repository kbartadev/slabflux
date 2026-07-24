# Blueprint: Conduits & Memory Arenas Architecture

## Architectural Overview
Communication between independent CPU processing threads completely rejects operating-system mutexes. It operates through rigidly constrained ring-buffer matrices providing O(1) resource reclamation.

## Header Mappings
- **`pool.hpp` / `spsc_pool.hpp`**: Exposes the `make()` factory method using pure perfect-forwarding to instantiate memory blocks statically. Demands manual `release()` execution at the end of the data lifespan.
- **`pinned_allocator_spsc.hpp`**: Implements the shadow-pointer high-watermark pattern. This allocator tracks producer/consumer epoch offsets to amortize atomic instructions locally, maximizing throughput when transferring payloads out of NUMA domains.
- **`conduit.hpp` / `spsc_conduit.hpp`**: The lock-free delivery channel strictly isolating pointer transmission (`try_push`, `pop`). Uses `std::memory_order_release` and `std::memory_order_acquire` over standard seq_cst to avoid hardware memory fences.