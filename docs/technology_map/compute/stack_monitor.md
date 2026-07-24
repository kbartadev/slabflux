# SlabFlux Compute: Stack Monitor (`stack_monitor.hpp`, `no_recursion_check.hpp`)

## 1. Architectural Overview
Deep call stacks and recursive functions dynamically allocate memory on the thread stack, blowing out the L1 Data Cache and violating the mathematical predictability of execution times. The `stack_monitor` and `no_recursion_check` modules provide static and runtime guarantees that execution graphs remain perfectly flat.

## 2. Static Recursion Fencing
Standard C++ compilers cannot always predict if a complex, multi-translation-unit graph is recursive.
SlabFlux utilizes the `no_recursion_check` template constraint:
- Handlers are topologically sorted via the `pipeline` SFINAE engine.
- If a handler attempts to re-emit an event that leads back to itself, the DAG evaluator mathematically detects the cycle at compile time and deliberately causes a Substitution Failure, aborting the build.

## 3. High-Watermark Stack Tracing
At runtime, the `stack_monitor` pre-allocates a fixed thread stack (e.g., via `pthread_attr_setstacksize` to a minimal 64KB) and places a hardware watchpoint (Magic Canary) at the boundary.
- If the `vector_lane_engine` attempts to allocate massive local arrays or enters an unexpectedly deep call chain, the stack pointer will hit the canary.
- The `integrity_validator` catches this instantly, halting the pipeline before a hard Stack Overflow segmentation fault destroys the process.

## 4. Forced Inlining
This module works symbiotically with the `SLAB_FORCE_INLINE` macros. By verifying that the compiler actually respected the inline directives (resulting in a shallow physical call stack), the system proves that all logical steps have been successfully flattened into a contiguous instruction stream.