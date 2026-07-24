# SlabFlux Core: Compute Logic (`compute_logic.hpp`)

## 1. Architectural Overview
The `compute_logic` header enforces the core ruleset for structuring deterministic business handlers within SlabFlux. It ensures that state representations remain strictly serializable and trivially copyable.

## 2. State Mutation Contracts
- Prohibits the use of internal heap allocations inside logic handlers.
- Validates memory alignments for the `vector_lane_engine` via `static_assert`.

## 3. Replay Saga Preservation
By guaranteeing that compute logic matrices are structurally flat, the engine guarantees that state can be snapshotted in O(1) time and fully restored for failover or simulation replays via the `replay_saga`.