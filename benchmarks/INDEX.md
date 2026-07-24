# � SLABFLUX Benchmarks

This directory contains the high-precision performance verification suite for the SLABFLUX Runtime Engine. These tests use [Google Benchmark](https://github.com/google/benchmark) to measure micro-latencies at the nanosecond and CPU-cycle level.

---


## ⏱️ Benchmarks

Benchmarks measure the raw CPU latency of the core `conduit` SPSC ring buffer, `pool` allocator, the **HTTP parser** and the **Hybrid SSO String Service**.

---

```text
Run on (12 X 3893 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x6)
  L1 Instruction 32 KiB (x6)
  L2 Unified 1024 KiB (x6)
  L3 Unified 32768 KiB (x1)
```

### ⚡ Low-Level Conduit Latency
These benchmarks measure the raw CPU cycles required for SPSC (Single-Producer Single-Consumer) operations and event routing.

### ⚙️ Micro-Latencies (Instruction-Level)

| Benchmark | Time (ns) | CPU (ns) | Iterations | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Conduit Push / 1024** | 1.84 | 1.84 | 407M | SPSC write |
| **Conduit Push / 4096** | 1.74 | 1.72 | 373M | — |
| **Conduit Push / 8192** | 1.70 | 1.69 | 407M | — |
| **Pipeline Dispatch** | 8.80 | 8.79 | 74M | Static CRTP Router |
| **FullFlux (push+pop)** | 11.4 | 11.5 | 64M | E2E Synchronization |

### � Throughput & Stress (Saturation)

| Benchmark | Time (ns) | CPU (ns) | Iterations | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **SPSC Throughput** | — | 2.03 ms | 100 | 4.92M msg/s |
| **Wrap-Around Test** | 10.66 ms | 10.42 ms | 90 | 1.06M msg/s |
| **False-Sharing Test** | 19.53 ms | 31.25 µs | 1000 | isolation |

### � String Service (Hybrid SSO) Benchmarks
The `slabflux::string_service` uses **Hybrid SSO** logic. Strings <= 48B incur zero allocation tax. Larger strings utilize deterministic slab fragmentation across the `slabflux::pool`.

| Benchmark | Latency (ns) | CPU (ns) | Throughput | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **String Assignment (32B)** | 3.25 | 3.21 | **9.28 Gi/s** | SSO Path (Zero Tax) |
| **String Assignment (48B)** | 3.43 | 3.45 | **12.95 Gi/s** | SSO Boundary Path |
| **String Assignment (64B)** | 3.98 | 3.99 | **14.93 Gi/s** | Pool Path (SIMD Peeling) |
| **String Assignment (1KB)** | 18.6 | 18.8 | **50.63 Gi/s** | Saturated Slab Write |

### � Protocol & Network Stack Benchmarks
Measured against a strict standard where every iteration includes a mandatory character-level integrity check to force pointer materialization and prevent compiler optimization.

| Benchmark Tier | Time (ns) | CPU (ns) | Iterations | Performance Notes |
| :--- | :--- | :--- | :--- | :--- |
| **HTTP Parser (Raw DFA)** | 28.6 | 28.6 | 22.4M | 34.9M parsed req/s |
| **Http Passthrough** | 30.9 | 30.5 | 23.5M | Structured Event |
| **SLABFLUX Zero-Copy Floor** | 43.3 | 42.2 | 14.9M | **Abs. User-Space Floor** |
| **Kernel Loopback Tax** | 3280.0 | 3296.0 | 0.21M | Standard OS Stack |

### Multi-Stage Dispatch & Synchronization Performance

### 1. Performance Methodology: Sustained Steady-State Performance
To ensure benchmark accuracy on high-frequency hardware (12-core @ 3.89 GHz), all tests enforce a **1.5s Warmup Period** (`MinWarmUpTime`). 

*   **Thermal Equilibrium:** Ensures the CPU has reached sustained clock speeds and power states.
*   **Cache Saturation:** Populates L1/L2/L3 caches with critical path instructions and event stack meta.
*   **Predictor Training:** Allows the Branch Target Buffer (BTB) to stabilize, providing a realistic view of how the architecture handles static vs. dynamic dispatch under load.

### 2. Micro-Latency: Static Matrix Fusion vs. Dynamic Inheritance
#### Architectural Baseline (Single-Threaded)
This benchmark compares the raw dispatch latency of 100 event stages.

| Implementation | Latency (ns) | CPU Time (ns) | Iterations |
| :--- | :--- | :--- | :--- |
| Legacy (Virtual Inheritance) | 189 ns | 188 ns | 3,733,333 |
| **SLABFLUX (Matrix Fusion)** | **145 ns** | **148 ns** | **4,977,778** |

**Analysis:**
The SLABFLUX implementation utilizes **Matrix Fusion** to allow the MSVC compiler to inline handlers and eliminate 100 virtual function calls. By replacing pointer-chasing with a contiguous instruction stream, we achieve a **23% reduction** in raw latency.

### 3. The Synchronization Tax: Single-Threaded Locking
#### Synchronized Baseline (Lock Overhead)
Adding `std::mutex` introduces an atomic floor. This test measures the efficiency of the dispatch logic when wrapped in a thread-safe critical section.

| Implementation | Latency (ns) | CPU Time (ns) | Overhead |
| :--- | :--- | :--- | :--- |
| Legacy (Synchronized) | 213 ns | 209 ns | +24 ns |
| **SLABFLUX (Synchronized)** | **155 ns** | **153 ns** | **+10 ns** |

**Analysis:**
SLABFLUX minimizes the "Mutex Tax." Because the internal logic is more efficient, the CPU spends less time managing the memory barriers associated with the lock, resulting in significantly lower overhead compared to the legacy approach.

### 4. System Resilience: Scalability Under Contention
#### Contention Scaling (1 to 12 Threads)
This test simulates production load by forcing multiple threads to fight for the event stack lock.

| Threads | Legacy (Real Time) | SLABFLUX (Real Time) | Throughput Gain |
| :--- | :--- | :--- | :--- |
| 1 Thread | 201 ns | 153 ns | +24% |
| 4 Threads | 210 ns | 173 ns | +18% |
| 12 Threads | 304 ns | **194 ns** | **+36%** |

**The Inversion Point:**
SLABFLUX's architectural efficiency is so high that its performance under **maximum contention (12 threads, 194 ns)** is nearly identical to the Legacy system's performance with **no threads and no synchronization (189 ns)**. 

**Conclusion:**
By reducing the time spent inside the critical section, SLABFLUX effectively prevents "Contention Collapse," allowing the system to maintain high throughput and low tail-latencies even as thread counts increase.

---

*All values measured on (12 X 3893 MHz CPU) with 48 KiB L1D / 32 KiB L1i caches.*
*All performance values are indicative measurements and may vary across hardware, compilers, and workloads.*
Compiled with MSVC v143 (Release). Tested on Windows 11 Pro, AMD Ryzen 5 9600X.


## � Benchmark Categories

---


### ⚡ Core Mechanics & Micro-Latencies

Fundamental measurements of the lock-free primitives and dispatch logic.

* **[Atomic Fences](./atomic_fences.cpp)**: Comparing Acquire-Release vs. Sequential Consistency overhead.

* **[Conduit Latency](./conduit.cpp)**: Measuring raw SPSC push/pop micro-latencies.

* **[Pipeline Dispatch](./pipeline_dispatch.cpp)**: Static CRTP-based event routing speed.

* **[Wrap-Around Test](./wrap_around.cpp)**: Stability and speed of ring buffer index resets.



### �️ Architectural Comparison (Matrix Fusion)

Proof of concept comparing modern static dispatch against legacy OOP patterns.

* **[Event Stack & Fusion](./event_stack.cpp)**: A 100-stage processing test comparing **Matrix Fusion** (static) against **Virtual Inheritance** (dynamic).



### ⚙️ Memory & Hardware Physics

Verification of O(1) determinism and hardware-level isolation.

* **[Cache-Line False Sharing](./cache_line_false_sharing.cpp)**: Validating the effectiveness of `alignas(64)` padding.

* **[Event Lifecycle](./event_destruction.cpp)**: Measuring the speed of O(1) slab acquisition and deallocation.

* **[String Service SSO](./string_service.cpp)**: Hybrid SSO throughput and SIMD "Loop Peeling" for larger fragments.



### � Network & Protocol Stack

Measuring the efficiency of protocol handling and the impact of the OS kernel.

* **[HTTP Parser DFA](./http_parser.cpp)**: Zero-copy DFA-based request parsing throughput.

* **[HTTP Passthrough](./http_passthrough.cpp)**: End-to-end structured event handling.

* **[Networked Conduit](./http_passthrough_networked_conduit.cpp)**: User-space processing floor for networked events.

* **[Kernel Tax](./http_passthrough_networked_conduit_kernel.cpp)**: Measuring the latency overhead introduced by the OS kernel loopback.



### � Analysis Tools

* **[Latency Histogram](./latency_histogram.cpp)**: Generating RDTSC-based p50, p99, and p99.9 latency distributions.

* **[Producer-Consumer](./producer_consumer.cpp)**: SPSC throughput under multi-threaded contention.



## �️ Build Infrastructure

Requires a strictly compliant C++20 compiler and CMake 3.20+

```bash

# Build the suite

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target benchmarks

# Run a specific benchmark (e.g., Conduit)

./benchmarks/bench_conduit

```
