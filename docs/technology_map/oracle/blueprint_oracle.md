# Blueprint: State Oracle

## Architectural Overview
External state bridging node. Ingests slow-moving external states (e.g., daily risk limits, FX rates) into the fast-path determinism via RCU (Read-Copy-Update) memory models without locking.