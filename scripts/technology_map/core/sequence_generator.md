# SlabFlux Core: Sequence Generator (`sequence_generator.hpp`)

## 1. Architectural Overview
In distributed meshes, timeline integrity relies heavily on monotonic progression. The `sequence_generator` acts as the unyielding atomic anchor, producing gap-less Logical Sequence Numbers (LSNs) that drive the entire deterministic `causal_mesh`.

## 2. Wait-Free Atomic Execution
To ensure that multiple parallel ingress threads (e.g., handling different NIC RX rings) do not stall while requesting a sequence ID, the generator utilizes `std::atomic_fetch_add_explicit`.
- Operating with `std::memory_order_relaxed`, it avoids expensive memory fencing instructions on the x86-64 silicon.
- The hardware simply increments the local cache line atomically, allowing sequence generation to occur in ~2 nanoseconds.

## 3. Structural Independence
The generator is intentionally isolated from the payload contents.
By cleanly decoupling sequence origination from data routing, the generator can act as the centralized authority for multiple distinct domains (Admin, Networking, Trading), mathematically guaranteeing that no two events across the entire node ever share the same temporal horizon.

## 4. Bootstrapping Recovery
Upon a node restart, the `sequence_generator` reads its initial state directly from the tail of the `durable_journal`. This guarantees that even after a complete power loss, the node resumes sequence production without accidentally replicating historical IDs or triggering `causal_mesh` quarantines.