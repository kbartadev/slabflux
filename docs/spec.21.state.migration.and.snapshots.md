# State Migration, Snapshots & Config

Managing the lifecycle, configuration matrix, and disaster recovery of a purely deterministic system necessitates highly specialized engines that operate strictly out-of-band to prevent stalling the hot path.

## State & Snapshot Engines
* **`slabflux::compute::snapshot_engine.hpp`**: Executes point-in-time, bit-exact memory snapshots of the entire running RTE state (including active memory pools, inflight conduits, and handler states) utilizing advanced wait-free RCU (Read-Copy-Update) techniques.
* **`slabflux::compute::state_observer.hpp`**: Provides a cache-isolated viewport, allowing background reporting threads to safely query hot-path execution state without triggering cache-line invalidation (False Sharing) on the primary processing cores.
* **`slabflux::core::state_migrator.hpp`**: The cornerstone of zero-downtime operations. It facilitates live cluster failovers by seamlessly transferring the bit-exact state captured by the `snapshot_engine` to a passive standby node over the network.

## Immutable Configuration Hierarchy
* **`slabflux::core::immutable_config.hpp` & `config_bridge.hpp`**: Configuration architectures are loaded exclusively during the `sovereign_init` boot phase and permanently sealed in memory. The `config_bridge` exposes this data to the hot path via direct memory offsets, ensuring configuration lookups possess zero pointer-chasing overhead (strict O(1)).
* **`slabflux::core::sovereign_schema.hpp`**: Enforces strict, statically-typed C++ schema definitions for all runtime configurations, outright eliminating the possibility of catastrophic runtime parsing failures.
