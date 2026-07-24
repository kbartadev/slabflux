# SlabFlux Compute: Execution Kernels (`kernels.hpp`)

## 1. Architectural Overview
The `kernels.hpp` module houses the metaprogrammed mathematical primitives for the `vector_lane_engine`. It enables the C++ compiler to fuse multi-step AI calculations (e.g., Matrix Multiplication -> ReLU -> Decay) into a singular, flattened CPU instruction stream.

## 2. Compile-Time Graph Fusion
Standard mathematical libraries execute step-by-step: load array, multiply, store array, load array, add, store array. This thrashes the L1 data cache and wastes clock cycles on memory access.
- The `execution_graph<...Ops>` evaluates the sequence of operations at compile time using SFINAE and Typelist expansion.
- It nests the operations sequentially, allowing the C++ compiler to emit Fused Multiply-Add (`_mm512_fmadd_ps`) instructions.
- The data never leaves the 512-bit ZMM CPU registers until the entire mathematical topology is resolved, achieving ultimate hardware efficiency.

## 3. Zero-Branch Thresholds
AI models frequently rely on activation functions like ReLU (Rectified Linear Unit), which conceptually require branching (`if x < 0 then 0`).
- The `relu_op` kernel avoids this branch entirely by using AVX-512 `_mm512_max_ps(v, _mm512_setzero_ps())`.
- This guarantees that all parallel vectors execute in the exact same number of cycles, regardless of the input data values.

## 4. Hardware Sizing
The kernels are template-bound to the hardware width (`__m512`, `__m256`, or scalar `float`). This allows the exact same code to compile for ultra-dense server processors or lightweight embedded IoT edge devices without altering the business logic.