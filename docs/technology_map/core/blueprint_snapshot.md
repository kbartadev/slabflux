# Blueprint: State Capture & Replay Architecture

## Architectural Overview
State persistence operates asynchronously, avoiding blocking the execution pipeline while ensuring that any historical state can be reconstructed with bit-perfect accuracy.

## Core Components
- **Snapshot Manager (`snapshot_manager.hpp`, `snapshot_engine.hpp`)**: Implements dual strategies for NVMe persistence. The primary engine maps state directly via `mmap` with `MAP_POPULATE` and `msync(MS_ASYNC)`. The secondary backend leverages Linux `io_uring` with `IORING_SETUP_SQPOLL` for true kernel-bypass gathered writes (`writev`).
- **Deterministic Replay (`replay_saga.hpp`)**: Reads the durable LSN-indexed journal linearly to rebuild the `vector_lane_engine` state. This process executes exactly the same CPU instructions that occurred during live execution, achieving 100% state reproduction without relying on a relational database.
- **Offset Resolution (`offset_ptr.hpp`)**: Escapes address-space randomization layout (ASLR) constraints by swapping absolute pointers for base-relative offsets, ensuring states remain valid across different physical machines upon reload.