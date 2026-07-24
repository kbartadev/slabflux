# SlabFlux Sys: FPU Shield (`fpu_shield.hpp`)

## 1. Architectural Overview
In ultra-low-latency deterministic execution, unexpected OS context switches are fatal. One of the most insidious sources of hidden latency is **Lazy FPU Context Switching** employed by modern Linux kernels.

When a thread executes its first floating-point or AVX-512 instruction, the kernel intercepts a hardware trap, halts the thread, and allocates/initializes the massive 2KB+ ZMM register state. The `fpu_shield` mathematically eliminates this penalty.

## 2. Hardware Register Ignition
During the `ignition_manifest` boot sequence, before the system connects to the live network, the `fpu_shield` physically forces the CPU to engage its vector units.
- It executes a series of dummy `_mm512_setzero_ps()` and `_mm512_add_ps()` instructions.
- This signals the kernel to instantly allocate the full `xsave`/`xrstor` state block for the current thread.
- Once the hot-path begins evaluating market data or AI matrices, the vector registers are already permanently engaged, completely eradicating the 10-20 microsecond trap penalty.

## 3. Downclocking Mitigation (AVX-512 Offset)
Historically, executing AVX-512 instructions caused severe CPU core frequency downclocking (the "AVX-512 Tax") as the silicon heated up.
The `fpu_shield` works in tandem with the `power_governor`:
- It utilizes the `thermal_soak` loops during initialization to intentionally drive the CPU thermal envelope to its peak load.
- By establishing the thermal equilibrium *before* live trading begins, the system forces the CPU into its stabilized AVX frequency band deterministically, ensuring that calculation latency remains mathematically flat rather than jittering as the CPU throttles mid-flight.

## 4. Denormals-Are-Zero (DAZ) Enforcement
Floating-point calculations involving numbers extremely close to zero (denormals) invoke hardware microcode assists, which can stall the pipeline for over 100 clock cycles per operation.
The `fpu_shield` explicitly configures the CPU's MXCSR register:
- Enables `_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON)`.
- Enables `_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON)`.
This forces the ALU to treat all subnormal numbers as absolute zero, guaranteeing branchless, single-cycle FMA execution.