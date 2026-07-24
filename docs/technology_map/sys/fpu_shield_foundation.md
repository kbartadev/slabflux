# Foundation: FPU Shield (`slabflux/sys/fpu_shield.hpp`)

## 1. Architectural Justification
In ultra-low-latency deterministic execution, unexpected OS context switches are fatal. One of the most insidious sources of hidden latency is **Lazy FPU Context Switching** employed by modern kernels, which halts the thread to allocate the massive 2KB+ ZMM register state upon the first SIMD instruction. The `fpu_shield` mathematically eliminates this penalty.

## 2. Hardware Implementation Directives
- **Hardware Register Ignition**: Executes dummy `_mm512_setzero_ps()` operations during the `ignition_manifest` boot sequence, forcing the kernel to instantly allocate the full `xsave`/`xrstor` state block before live trading begins.
- **Thermal Soak Equilibrium**: Reaches peak thermal load deliberately prior to ignition. This forces the CPU into its stabilized AVX frequency band (paying the "AVX-512 Tax" upfront), ensuring calculation latency remains mathematically flat during live execution.
- **Denormals-Are-Zero (DAZ) Enforcement**: Configures the MXCSR register (`_MM_SET_FLUSH_ZERO_MODE`, `_MM_SET_DENORMALS_ZERO_MODE`) to force the ALU to treat all subnormal numbers as absolute zero, neutralizing microcode assist stalls (which can cost 100+ cycles per operation).

## 3. Bibliography & Proofs
1. **Intel Corporation**. *Intel 64 and IA-32 Architectures Optimization Reference Manual*. (MXCSR register, DAZ/FTZ enforcement, and Subnormal arithmetic penalties).
2. **Fog, Agner**. (2021). *Optimizing subroutines in assembly language*. (AVX frequency scaling and Lazy FPU Context Switching).
3. **Corbet, J.** (2014). *Lazy FPU restore: the end of an era*. LWN.net.