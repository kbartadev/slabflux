# SlabFlux Security: Semiotic Tapestry (`semiotic_tapestry.hpp`)

## 1. Architectural Overview
The `semiotic_tapestry` acts as the overarching telemetry fabric that connects isolated Aphasic Horizon events to the system's global cognitive awareness. It is the intelligence layer that decides whether a dropped packet is a random network anomaly or a coordinated systemic attack.

## 2. The Fray Aggregator
When a module invokes the `aphasic_horizon_` (e.g., dropping a packet due to an invalid LSN or fractured SRF signature):
- It emits a tiny, 8-bit `fray` code.
- The tapestry weaves these microscopic anomalies into a sliding window matrix (the Fray Ledger).

## 3. Pattern Recognition
The tapestry monitors the velocity and density of the frays:
- If it detects a dense cluster of `0x51` (Journal Write Failure) frays, it deduces that the NVMe drive is saturated and triggers a backpressure signal to the `flow_controller`.
- If it detects scattered `0x0D` (Divergence Analyzer) frays, it deduces hardware RAM degradation and initiates an unmaskable MCE shootdown.

## 4. Sovereign Resilience
By evaluating the macro-structure of micro-failures, the Tapestry prevents the system from overreacting to singular glitches. It grants the engine the resilience to seamlessly power through isolated packet drops while instantly recognizing true architectural collapse.