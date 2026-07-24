# SlabFlux Math: Fixed-Point Mathematics (`fixed_math.hpp`)

## 1. Architectural Overview
In distributed state replication, floating-point arithmetic (`float`, `double`) is incredibly dangerous. Different CPU architectures (e.g., AMD Zen 4 vs. Intel Skylake), or even the same CPU using different instruction sets (x87 FPU vs. AVX2), can produce microscopic rounding discrepancies. Over millions of iterations, these errors compound, causing the causal mesh to diverge and fracture.

The `fixed_math` module enforces 100% deterministic, cross-platform arithmetic by utilizing fixed-point integers to represent fractional values.

## 2. Integer-Backed Precision
The system utilizes a base integer type (usually `int64_t`) combined with a compile-time scaling factor (e.g., `10^8` for ultra-precise financial increments).
- **Addition/Subtraction**: Handled via standard, single-cycle integer ALU operations. No rounding errors exist.
- **Multiplication/Division**: Requires bit-shifting and double-wide integer upcasting (`__int128`) to prevent overflow during intermediate calculations.

## 3. Compile-Time Scaling (Metaprogramming)
To prevent runtime division overhead when converting between different fractional bases:
- The `fixed_point<T, Scale>` template forces the compiler to pre-calculate normalization constants.
- Multiplying a `fixed_point<int64_t, 1000>` by a `fixed_point<int64_t, 100>` scales perfectly via integer arithmetic, without invoking unpredictable floating-point emulation.

## 4. Hardware Optimization
Because `fixed_math` relies purely on `int32_t` and `int64_t`, it can be mapped perfectly to AVX2/AVX-512 integer execution units (`_mm512_add_epi64`, `_mm512_mullo_epi64`). This allows the `vector_lane_engine` to calculate exact financial pricing models or portfolio risk matrices in parallel, with zero mathematical drift across the entire cluster.