# SlabFlux RTE: Pipeline Warmer (`pipeline_warmer.hpp`)

## 1. Architectural Overview
In High-Frequency Trading, markets experience periods of extreme volatility followed by periods of absolute silence. During these quiet intervals, the CPU's Branch Target Buffer (BTB) loses its predictions, and the L1 Instruction Cache (I-Cache) can evict the trading logic. The first packet to arrive after a silence will experience a massive latency spike (a "Cold Hit").
The `pipeline_warmer` systematically prevents hardware sleep states and cache eviction by generating synthetic load.

## 2. Ghost Event Injection
During idle polling cycles where no network packets arrive, the `round_robin_poller` invokes the pipeline warmer.
- It generates "Ghost Events"—structurally valid, 64-byte aligned dummy payloads.
- These payloads traverse the entire `pipeline` dispatch matrix, unrolling the handlers and executing the `static_cast` SFINAE logic.

## 3. State Isolation
To prevent the Ghost Events from actually executing trades or mutating AI matrices:
- Handlers are strictly guarded. The ghost event carries a specific `is_synthetic` bitflag.
- Alternatively, they are routed exclusively through `constexpr if (!is_ghost<Event>())` evaluation paths within the domain logic.

## 4. Hardware SMT Training
By continuously streaming ghost instructions through the ALUs, the CPU's Branch Predictor Unit (BPU) remains perfectly trained for the exact sequence of assembly instructions representing the trading hot path. The silicon remains physically locked in the C0 (Maximum Performance) P-state, ensuring 0 microsecond wake-up latency when real market data resumes.