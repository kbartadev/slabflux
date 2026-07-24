# Deterministic AI & Cognitive Stimulus

The `slabflux::ai` namespace empowers the framework to execute machine learning inference models directly on the critical hot path, entirely bypassing the non-deterministic overhead associated with traditional AI runtimes.

## `slabflux::ai::deterministic_ai_core`
A highly optimized, zero-allocation inference engine designed for pre-trained network weights.
* **In-Memory Models:** AI model weights are loaded exclusively into cache-pinned, aligned memory slabs during the `Ignition` phase. This guarantees absolutely zero disk I/O occurs during live market inference.
* **SIMD Inference:** Leverages the `vector_lane_engine` to execute massive matrix multiplications and neural activations in deterministic O(1) time.
* **Branchless AI:** The mathematical evaluation pathways are engineered to strictly avoid conditional branching, maintaining an unconditionally flat jitter profile across the CPU pipeline.

## `slabflux::ai::cognitive_stimulus`
A specialized, high-velocity event injector responsible for feeding high-dimensional telemetry data into the AI core.
* **Causal Injecting:** Enables the AI engine to continuously ingest and learn from the distributed `causal_mesh` state in real-time.
* **Data Transformation:** Functions as an ultra-low-latency pipeline that mutates raw `tick_event` signals into the precise, aligned tensor formats mandated by the `deterministic_ai_core`.
