# Blueprint: Hardware Topology Architecture

## Architectural Overview
Software layout operates subservient to hardware mechanics. Data and functional blocks are orchestrated explicitly to maintain cache-line sovereignty and maximize the utilization rates of L1 instruction and data caches.

## Core Components
- **Cache Sovereignty (`cache_shield.hpp`, `alignment_checks.hpp`)**: Enforces `alignas(64)` boundaries strictly mapped against `std::hardware_constructive_interference_size`. Consteval validators (`layout_verifier`) flag accidental memory blending at compile time.
- **NUMA Locality & Thread Pinning (`hardware_topology.hpp`, `memory_topology.hpp`)**: Utilizes kernel hooks (`pthread_setaffinity_np` and NUMA policies like `mbind`) restricting threads and corresponding buffer allocations locally to their origin socket, preventing Infinity Fabric/QPI bridge saturation.
- **I-Cache Pinning (`hot_path_alignment.hpp`, `instruction_shield.hpp`)**: Leverages deep compiler hint attributes (`SLAB_HOT`, `__attribute__((flatten))`, `_Pragma("clang loop unroll")`) forcing compilation into contiguous instruction segments separated from initialization binaries.
- **Memory Integrity Verifiers (`integrity_validator.hpp`, `integrity_guard.hpp`)**: Introduces poison-block magic thresholds (`0xCAFEBABE`, `0xDEADBEEF`) around active regions. Conducts inline SSE4.2 hardware-accelerated payload CRC32 validation without latency penalties.
- **Telemetry Signals (`nanoscope_bridge.hpp`, `chip_telemetry.hpp`)**: Integrates isolated lock-free rings exclusively for telemetry. Utilizes raw hardware timestamps (`__rdtsc()`) substituting generic OS `chrono` equivalents for zero-syscall, sub-nanosecond observability.