# SlabFlux I/O: Clock Node (`clock_node.hpp`)

## 1. Architectural Overview
In deterministic engines, querying `std::chrono::system_clock::now()` inside the business logic destroys absolute replayability because the OS clock is non-deterministic and subjected to NTP slewing.
The `clock_node` serves as the authoritative, hardware-anchored temporal heart of the SlabFlux architecture.

## 2. The Sovereign Tick
The `clock_node` operates as a standalone producer on the `TimeBus`.
- It periodically reads the CPU's physical Time Stamp Counter (`__rdtsc()`).
- Once the delta reaches a predefined resolution (e.g., 100 microseconds), it drops a strictly sequenced `sys::tick_event` into the `spsc_conduit`.

## 3. Replayable Temporality
Because the `branchless_engine` receives time exclusively via these deterministic queue events (exactly like network data):
- Time becomes a perfectly reproducible sequence of state mutations.
- During a failover replay, the `replay_saga` re-injects historical `tick_event` structures. The mathematical algorithms execute exactly as they did originally, totally immune to the current physical time of the recovery server.