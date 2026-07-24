# SlabFlux Workflow: Deterministic Saga (`state_machine.hpp`)

## 1. Architectural Overview
The `deterministic_saga` is a zero-allocation, transition-table based state machine. It guarantees that multi-stage workflow transitions (e.g., order lifecycle management) execute in `O(1)` time without memory fragmentation or hash-map lookup costs.

## 2. Linear Cache-Aligned Lookup
The engine pre-allocates an array of `saga_slot` entities up to `MaxConcurrentWorkflows`.
- Lookup is executed strictly using bitmask indices: `id & (MaxConcurrentWorkflows - 1)`.
- Because it is a contiguous, flat `std::array`, resolving a workflow context triggers exactly zero cache misses.

## 3. Teleological Aphasia Integration
If an inbound event targets a workflow ID that exceeds the hardware boundary bounds:
- It does not trigger a runtime exception or standard bounds failure.
- It triggers the `execute_void_transition` via the Aphasic Horizon.
- The `semiotic_tapestry` instantly engraves the fault coordinate into the CPU's Last Branch Record (LBR), discarding the invalid state operation cleanly.