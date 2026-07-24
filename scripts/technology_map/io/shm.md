# Blueprint: Inter-Process Communication (IPC) Matrices Architecture

## Architectural Overview
The IPC matrix operates strictly via cache-aligned Shared Memory mapped across OS boundaries, moving data between separate process segments with 0-copy pointers and sub-microsecond latency.

## Header Mappings
- **`shm_bridge.hpp`**: The primary atomic matrix linking isolated processes (`ipc_role::creator` / `ipc_role::joiner`). Connects SPSC state via Linux `shm_open`.
- **`shm_ingress.hpp` / `shm_egress.hpp`**: Bridge wrappers translating network burst mechanics (`reserve_at`, `commit_n`) back into C++ typed events compatible with localized `spsc_conduit` structures.
- **`shm_duplex.hpp`**: Exposes bidirectional, lock-free matrices enabling continuous state synchronization loops between independent application boundaries.
- **`shm_arena_duplex.hpp`**: Neutralizes Address Space Layout Randomization (ASLR). Dynamically maps absolute pointer memory addresses to deterministic base-offsets relative to the arena boundaries.
- **`shm_journal_duplex.hpp`**: Provides stateful continuity logic when recording SHM state frames dynamically against continuous virtual-memory offsets.
- **`shm_inline_duplex.hpp`**: Bypasses the CPU L1/L2 caches when injecting small payloads into the IPC pipe using AVX-512 non-temporal instruction stores (`_mm512_stream_si512`), preventing primary trading-logic cache pollution.