# SlabFlux Workflow: Sovereign Saga (`sovereign_saga.hpp`)

## 1. Architectural Overview
In distributed high-frequency environments, multi-stage transactions (like a distributed trade execution across matching engines) cannot use blocking Two-Phase Commit (2PC) protocols, as they cause massive systemic latency stalls.

The `sovereign_saga` implements the Saga pattern deterministically: it breaks long-running transactions into a sequence of local, wait-free operations, coupled with mathematically inverse "compensating actions" to handle rollbacks.

## 2. Deterministic Rollback Mechanics
If Step 3 of a transaction fails due to validation rejection or timeout:
1. The Saga engine instantly pauses forward execution.
2. It executes the compiled compensating actions for Step 2 and Step 1 in reverse topological order.
3. Because the execution is entirely tied to the `hlc_clock` and `lsn_heartbeat`, the rollbacks are appended to the causal sequence just like forward events, guaranteeing that replica nodes achieve the exact same reverted state simultaneously.

## 3. Zero-Allocation Saga State
Unlike enterprise Sagas that serialize state to databases mid-flight, the `sovereign_saga` manages state exclusively via lock-free rings (`mpmc_conduit`). 
- Pending sagas are held in a bounded array within L1/L2 cache limits.
- Timeout evaluation is handled natively by the `timing_wheel` rather than background sleeper threads, ensuring microsecond precision for rollback triggers.

## 4. Failure Isolation
The engine mathematically guarantees that a failed Saga will not poison the global state. All intermediate mutations are tagged with provisional flags until the final step emits the global commit marker.