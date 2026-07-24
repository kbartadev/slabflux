# SlabFlux Core: FPU Shield (`fpu_shield.hpp`)

## 1. Architectural Overview
The Floating Point Unit (FPU) in modern processors requires careful management to avoid "Lazy FPU Context Switching" latencies. The `fpu_shield` eradicates this OS-level penalty.

## 2. Hardware Ignition
During initialization, the shield forces the CPU to allocate `xsave`/`xrstor` state blocks via dummy AVX addition routines before live execution starts, preventing the OS from interrupting the first hot-path vector operation.

## 3. DAZ / FTZ Enforcement
Configures the MXCSR hardware register to flush subnormal floats to absolute zero (Denormals-Are-Zero and Flush-To-Zero).
- Floating-point math on extremely small numbers (subnormals) requires complex microcode fallback loops inside the CPU.
- FTZ guarantees branchless, single-cycle ALU execution when numerical drift approaches zero, preserving deterministic cycle budgets.