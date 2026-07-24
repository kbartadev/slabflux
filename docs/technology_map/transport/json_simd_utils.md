# SlabFlux Transport: JSON SIMD Utilities (`json_simd_utils.hpp`)

## 1. Architectural Overview
Parsing and serializing JSON safely requires scanning for structural delimiters (`"`, `\`) and control characters (`< 32`) as mandated by RFC 8259. Doing this character-by-character introduces severe scalar loop bottlenecks.

The `json_simd_utils` module provides a purely stateless, hardware-accelerated scanning core utilized by both the `json_producer` and `baremetal_json_parser` to evaluate up to 64 bytes of JSON data per CPU cycle.

## 2. Overlapping Mask Vectors
Standard SIMD string scanning falls back to a scalar `while` loop when the remaining string is shorter than the vector width. 
SlabFlux explicitly prohibits this via **Overlapping Unaligned Loads**:
- **AVX-512**: Utilizes `_mm512_maskz_loadu_epi8` with a dynamically calculated bitmask (`(1ULL << shift) - 1`) to natively suppress page faults on the tail end without leaving the vector domain.
- **AVX2**: Performs a backward-shifted load (`len - 32`), re-scanning bytes that were already processed. The results are mathematically shifted out of the final bitmask, eliminating the need for scalar loops while strictly bounding execution time.

## 3. The `vptest` Optimization (`_mm_testz`)
Extracting a mask from an AVX register to a General Purpose Register (GPR) using `vpmovmskb` (`_mm256_movemask_epi8`) has a latency of 2-3 cycles.

To achieve maximum throughput, the utility avoids extracting the mask unless a match is guaranteed:
```cpp
__m256i cmp = _mm256_or_si256(_mm256_or_si256(eq_q, eq_s), eq_c);
if (SL_UNLIKELY(!_mm256_testz_si256(cmp, cmp))) {
    return pos + __builtin_ctz(_mm256_movemask_epi8(cmp));
}
```
The `_mm256_testz_si256` instruction maps to the hardware `vptest` instruction, which directly sets the CPU's Zero Flag (ZF). This allows an instantaneous branch prediction jump, keeping the pipeline saturated.

## 4. Arithmetic Bounding for Control Characters
Scanning for control characters (ASCII `< 32`) traditionally requires range checks. 
The utility executes this branchlessly across 32 lanes using `_mm256_max_epu8`:
```cpp
const __m256i v_31 = _mm256_set1_epi8(31);
__m256i eq_c = _mm256_cmpeq_epi8(_mm256_max_epu8(v_data, v_31), v_31);
```
If any character is less than or equal to 31, the `max` operation clamps it to 31. The subsequent equality check against 31 creates a perfect mask of all control characters in a single cycle.