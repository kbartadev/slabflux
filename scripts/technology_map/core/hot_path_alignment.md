# SlabFlux Core: Hot Path Alignment (`hot_path_alignment.hpp`)

## 1. Architectural Overview
In sub-microsecond trading architectures, where code is placed in physical memory is just as important as the code itself. The `hot_path_alignment` header serves as the foundational compiler-directive matrix, dictating exact Instruction Cache (I-Cache) boundaries, inline flattening, and hardware stalling behaviors to ensure zero-jitter execution.

## 2. Linker Section Sovereignty
Modern compilers typically scatter compiled functions across the binary, leading to L1 I-Cache misses when the execution jumps between them.

### `.text.hot` Segregation
By utilizing the `SL_SECTION_HOT` and `SLAB_ATTR_HOT` attributes, SlabFlux forces the GCC/Clang linkers to pack the critical path functions (like `critical_path_step`) sequentially into the exact same memory page. 
- When the CPU fetches the first instruction of the ingress polling loop, the entire execution matrix is physically loaded into the L1 I-Cache in a single burst, completely eliminating instruction fetch stalls.

### Expert Code Partitioning
The `SLAB_EXPERT_HOT(name)` macro allows developers to assign specific domain logic nodes to distinct, 64-byte aligned linker sections (`.text.expert.*`). This ensures that mathematically disparate AI models or trading algorithms never share an I-Cache line, preventing cache eviction crosstalk.

## 3. Loop Flattening (`SLAB_FLAT_PATH`)
Traditional `for` loops introduce backward jumps, stressing the Branch Target Buffer (BTB). 
The `SLAB_FLAT_PATH` macro utilizes Clang/GCC `#pragma unroll(full)` to physically flatten loops at compile time. It explicitly disables auto-vectorization and interleaving on control-plane loops, ensuring the compiler honors the hand-written AVX-512 structures without injecting unpredictable generic vector logic.

## 4. Hardware Halts
Standard error handling (`std::terminate` or `throw`) bloats the binary with stack-unwinding code, destroying L1 instruction density.
The `SLAB_HARDWARE_HALT()` macro (`__builtin_trap()` on Linux, `__debugbreak()` on Windows) generates a direct `SIGTRAP`. It instantly stops the CPU core without executing a single cleanup instruction, allowing external debuggers to capture the exact register state at the moment of fatal divergence.