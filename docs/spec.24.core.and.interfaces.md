# Core & Main Interfaces

This layer defines the absolute entry points and foundational API contracts that bind the architecture into a cohesive, deterministic execution engine.

## `slabflux::core::core`
The master orchestration singleton. It operates as the absolute dictator of the runtime environment.
* **The Ignition Sequence:** Executes the critical boot phase where it formally claims all `hugetlbfs` memory slabs, permanently pins execution threads via `hardware_topology`, locks the L3 cache using the `cache_partitioner` (Intel CAT), and formally boots the `durable_journal`.
* **Teardown Mechanics:** Dictates the graceful shutdown sequence. It enforces the clean flushing of all inflight conduits and coordinates the safe dismantling of lock-free memory pools to strictly guarantee zero data corruption.

## `slabflux::core` (The Root Interface)
The `core.hpp` header serves as the monolithic inclusion nexus for downstream application engineers. By carefully aggregating only the most critical, heavily utilized primitives (`pool`, `allocated_event`, `conduit`), it guarantees an uncluttered, pristine developer experience without necessitating the manual inclusion of deeply nested hardware headers.
