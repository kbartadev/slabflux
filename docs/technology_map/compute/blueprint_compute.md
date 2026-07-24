# Blueprint: Compute Architecture

## Architectural Overview
The SlabFlux Compute Subsystem handles high-density, deterministic mathematical processing. It separates control-plane routing from data-plane number crunching, leveraging extreme Instruction-Level Parallelism (ILP) and explicit Vectorization (AVX-512/AVX2) to mutate massive state vectors simultaneously.

## Core Components
- **Vector Lane Engine (`vector_lane_512.hpp`)**: Standard scalar arrays fail to fully utilize the silicon's Arithmetic Logic Units (ALUs). Tracks large arrays of 32-bit integer states using explicit software pipelining to saturate the CPU's FMA ports.
- **Cache-Line Streaming (`sync_state`)**: Utilizes Non-Temporal stores (`_mm512_stream_si512`) to flush computed results directly to Main Memory, completely bypassing the L1/L2 caches and preventing cache-pollution.
- **Physics Reactor (`physics_reactor.hpp`)**: Designed for predictive modeling and calculating confidence matrices at hyper-frequency. Utilizes hardware FMA (`_mm512_fmadd_ps`) to eliminate sequential add-then-multiply rounding errors.
- **Replay Saga Engine (`replay_saga.hpp`)**: Recovers state by mapping (`mmap(MAP_POPULATE)`) the durable NVMe log and streaming bit-perfect payloads directly into the vector engine, recovering millions of states instantly.
- **Non-Temporal Writer (`non_temporal_writer.hpp`)**: Identifies physical vector width at compile-time and statically maps to either AVX-512 or AVX2 instructions, ensuring hardware portability without branch overhead.