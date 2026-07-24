# SlabFlux Sys: Liveness Watchdog (`liveness_watchdog.hpp`)

## 1. Architectural Overview
In non-deterministic architectures, deadlocks or infinite loops lock up the process until an external ping fails. The `liveness_watchdog` and `temporal_guard` enforce a rigid, cycle-exact budget on every executing handler to prevent latency accumulation.

## 2. Supervisor Thread Observation
The watchdog executes entirely out-of-band on an isolated "Supervisor" core.
- It continuously compares the active `__rdtsc()` against a shared, lock-free `shared_tsc_marker_` written by the Compute core.
- If the delta exceeds the designated budget (e.g., 3,000,000 cycles / ~1ms), the supervisor recognizes a stall.

## 3. In-Band Temporal Guard
The `temporal_guard` operates directly within the Compute node. Upon processing a tick, it measures the elapsed physical time since the last action. If a specific routing function exceeded the deterministic bounds due to cache thrashing or a logic bug, the guard triggers immediately.

## 4. Deterministic Panic
When an overrun occurs, the engine does not log an error and attempt to resume, which would induce cluster-wide backpressure.
- It engraves the `0xDE` anomaly code into the silicon LBR using the `semiotic_tapestry`.
- It engages Structural Oblivion (`while(true) _mm_pause()`), permanently retiring the thread and instantly forcing the hardware Cluster Orchestrator to route traffic to the failover node.