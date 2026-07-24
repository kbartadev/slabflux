# Blueprint: Core Concurrency Architecture

## Architectural Overview
The SlabFlux concurrency architecture fundamentally rejects operating system-level synchronization primitives. Mutexes, locks, and blocking queues are substituted by strictly bounded, raw-pointer moving conduits and spatial diffusion matrices.

## Core Components
- **Distributed Conduits (`mpmc_sharded_conduit.hpp`, `mpmc_conduit.hpp`)**: Disconnects metadata (atomic tickets) from the payload into distinct arrays. Contention is shattered across physical NUMA boundaries using identity-mapped lane resolution.
- **Spatial Diffusion Matrices (`spatial_diffusion_conduit.hpp`, `ordered_diffusion_conduit.hpp`)**: Escapes central head/tail atomics. It allows threads to sweep parallel memory sectors using hardware AVX2/AVX-512 masking (`_mm256_movemask_epi8`), eradicating inter-core read-for-ownership (RFO) stalls.
- **Backpressure Regulation (`backpressure_valve.hpp`, `deterministic_policer.hpp`)**: Protects the computation pathways from network saturation using an instruction-clock synchronized Token Bucket. Rate limiting guarantees uninterrupted throughput independently of OS scheduling variations.
- **Zero-Copy Routers (`round_robin_switch.hpp`, `round_robin_poller.hpp`)**: O(1) Fan-in and Fan-out dispatch mechanisms replacing complex branching logic with simple numeric rotation calculations, shielding the CPU branch predictor.
- **Jitter Reordering (`hole_puncher.hpp`)**: Freezes pipeline progression dynamically based on `wire_frame_lsn` tags, forcing deterministic reconstruction upon sequenced packets arriving misaligned.
- **High-Velocity Streaming (`non_temporal_writer.hpp`)**: Safely purges processed blocks out of L1-L3 cache spaces straight to memory using AVX non-temporal instruction streams (`_mm512_stream_si512`), preserving hot-path memory bandwidth.