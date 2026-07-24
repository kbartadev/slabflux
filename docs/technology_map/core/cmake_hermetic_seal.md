# SlabFlux Build: Architecture & Verification (`CMakeLists.txt`)

## 1. Architectural Overview
The SlabFlux build system is designed around extreme performance compilation and mathematical verification. It relies on CMake to orchestrate strict dependency resolution, compiler flag optimization (LTO, AVX-512), and the compilation of the external Metadata Compiler (MOC).

## 2. Verification-Driven Compilation
SlabFlux provides a comprehensive test suite to mathematically prove routing logic correctness without enforcing restrictive build locks.
- **Adversarial Testing**: The build system compiles `meta_compiler_test` as a standard test target.
- **Flexible Agility**: The `slabflux_meta` compiler target builds independently of test validation stamps, removing rigid workflow blockers while retaining the ability to manually invoke strict adversarial checks via standard test runners (`ctest`).

## 3. High-Performance Dependencies
To achieve zero-syscall, bare-metal latency, the CMake configuration enforces linkage against bleeding-edge Linux network stacks:
- **Kernel-Bypass I/O**: Strict dependencies on `libxdp`, `libbpf`, and `libdpdk` for 100Gbps line-rate ingress.
- **Asynchronous Disk/Network**: `liburing` for zero-syscall SQPOLL disk journaling and socket transmission.
- **NUMA Topologies**: `numa` libraries for pinning threads and allocating memory directly on the PCIe-adjacent memory controllers.

## 4. Compiler Optimization Directives
- **Linux (GCC/Clang)**: Aggressive `-march=native -O3` flags ensure the compiler uses the absolute maximum instruction set available on the local silicon (AVX-512, BMI, WAITPKG).
- **Windows (MSVC)**: Employs `/O2`, `/GL` (Whole Program Optimization), and `/arch:AVX2` to extract maximum performance from the MSVC backend while isolating Linux-only paths (`io_uring_ingress.cpp`).