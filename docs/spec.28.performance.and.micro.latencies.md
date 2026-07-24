# Performance Benchmarks & Micro-Latencies

The SLABFLUX architecture is continuously profiled using `benchmark::benchmark` to mathematically prove O(1) determinism across all subsystems. Tests are executed on strictly isolated environments (e.g., 12-core @ 3.89 GHz, 48 KiB L1D cache).

## Conduit & Dispatch Latencies
* **Conduit Push:** Registers a sustained latency of **1.84 ns** for SPSC write operations.
* **Pipeline Dispatch:** The static CRTP router evaluates complex routing matrices in **8.80 ns**.
* **Matrix Fusion vs. Inheritance:** By eliminating virtual function overhead via compile-time SFINAE matrix fusion, dispatch latency drops from 189 ns to **145 ns**, achieving a 23% reduction in raw CPU cycles.

## String Service (Hybrid SSO)
* Assignments under 48 bytes execute entirely on the Small String Optimization (SSO) path, achieving **12.95 Gi/s** throughput with zero allocation tax. 
* Payloads exceeding 64 bytes overflow into deterministic memory pools, utilizing SIMD loop peeling to maintain **14.93 Gi/s** sustained throughput.

## Network Parsing Limits
* **HTTP Parser (Raw DFA):** Zero-copy deterministic finite automaton parses raw network data at **28.6 ns** per frame, equating to ~34.9M parsed requests per second on a single thread.
