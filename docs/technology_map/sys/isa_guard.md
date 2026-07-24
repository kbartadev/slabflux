# SlabFlux Sys: ISA Guard (`isa_guard.hpp`)

## 1. Architectural Overview
The deterministic execution matrix of SlabFlux relies heavily on modern x86-64 silicon extensions (AVX-512, BMI, WaitPKG) to bypass OS locks and achieve O(1) latency. 
The `isa_guard` serves as the foundational validation layer, interrogating the physical CPU die to ensure all required Instruction Set Architecture (ISA) features are present before allowing the core to ignite.

## 2. Silicon Feature Validation
The guard utilizes native `__get_cpuid` and `__get_cpuid_count` intrinsics to query the CPU's leaf and subleaf capabilities natively at runtime.

It accurately detects:
- **Core Vectors**: `SSE4.2`, `AVX2`, and `AVX-512F` for establishing the correct `vector_lane_engine` width.
- **Sovereign Wait States**: `WAITPKG` (TPAUSE, UMONITOR) which enables the `round_robin_poller` to yield execution ports without incurring C-State sleep penalties.
- **Cryptographic Entropies**: `RDRAND` and `SHA-NI` for seating the deterministic RNG engines.
- **Observability Tracks**: Architectural Last Branch Records (LBRs) for zero-overhead performance tracing.

## 3. Ignition Constraints and Tiering
The `verify_requirements()` function enforces the physical requirements of the deployment environment.
- If the core is operating in strict HFT constraints, a missing `WAITPKG` or `AVX-512` flag can immediately `std::terminate` the process, preventing degraded trading logic execution.
- Conversely, the system can utilize the ISA report to automatically scale its logic tiers (e.g., seamlessly downshifting the `deterministic_ai_core` from 512-bit ZMM registers to 256-bit YMM registers) ensuring full functionality across a broader array of heterogeneous cloud hardware.