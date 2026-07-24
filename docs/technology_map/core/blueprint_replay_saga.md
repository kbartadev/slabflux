# Blueprint: replay_saga.hpp

## Architectural Overview
Deterministic state reconstruction engine. Ingests NVMe journals using non-temporal reads and feeds historical network and clock events to the compute core to achieve exact-match state recovery.