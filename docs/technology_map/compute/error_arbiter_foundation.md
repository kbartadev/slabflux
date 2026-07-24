# Foundation: Error Arbiter (`slabflux/compute/error_arbiter.hpp`)

## 1. Architectural Justification
Standard applications `throw` exceptions, which unwind the stack, invoke dynamic memory allocations, and break deterministic time budgets. The `error_arbiter` replaces exceptions with a wait-free, lock-free quarantine ring, ensuring the engine can survive massive data corruption events without stalling.

## 2. Hardware Implementation Directives
- **Atomic Fault Records**: The arbiter accepts a 16-byte `fault_record` (containing the error code, LSN, and severity).
- **SPSC Fault Quarantine**: Errors are injected into an isolated `spsc_conduit` connecting the hot-path to the background `telemetry_node`.
- **Teleological Agnosia**: If the quarantine ring fills up, the `error_arbiter` executes a `_mm_pause` yield or routes the failure directly to the `aphasic_horizon_`. The engine completely ignores the corrupted state and immediately processes the next tick.

## 3. Bibliography & Proofs
1. **LMAX Exchange**. (2011). *The LMAX Architecture*. (Exception handling in Ring-Buffer architectures).
2. **C++ Core Guidelines**. (2023). *E.19: Use a throw-free error handling policy for hard real-time systems*.
3. **Hennessy, J. L., & Patterson, D. A.** (2017). *Computer Architecture*. (The micro-architectural cost of stack unwinding).