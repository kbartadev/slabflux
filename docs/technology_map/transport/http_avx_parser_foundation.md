# Foundation: SIMD-Accelerated HTTP Lexer (`http_avx_parser`)

## 1. Architectural Justification
Standard HTTP parsers evaluate protocol streams character-by-character using scalar `while` loops to identify delimiters (`\n`, `:`). In HFT environments, looping over unpredictable strings forces the CPU to evaluate millions of branches, leading to pipeline stalls on every misprediction.

The `http_avx_parser` replaces character iteration with **SIMD Hardware Sieving**, loading up to 64 bytes at a time and evaluating delimiters and control boundaries entirely within parallel vector lanes.

## 2. Hardware Implementation Directives
- **Vectorized Range Validation:** Enforcing RFC 9110 character bounds (rejecting control characters) is executed using unsigned integer underflow tricks expanded across AVX-512 lanes. A single `_mm512_cmpgt_epi8_mask` isolates bad bytes in 64 characters simultaneously.
- **Branchless Lowercasing:** Protocol case-insensitivity (`ieq` / `icontains`) uses arithmetic bitwise `OR` masking (`0x20`) instead of `if (c >= 'A')`. This normalizes strings purely via register arithmetic.
- **vptest Jump Acceleration:** Moving vector masks to general-purpose registers (GPRs) using `vpmovmskb` incurs a latency penalty. The lexer uses `_mm256_testz_si256` (`vptest`) to natively trigger the CPU Zero Flag (ZF), skipping blocks 32 bytes at a time with zero data movement if no delimiter exists.

## 3. Bibliography & Proofs
1. **Langdale, G., & Lemire, D.** (2019). *Parsing Gigabytes of JSON per Second*. VLDB Journal. (The seminal academic proof that branchless SIMD instructions can parse structural topologies at RAM bandwidth limits).
2. **Mula, T.** (2018). *SIMD-friendly algorithms for substring searching*. arXiv. (Demonstrating how `vptest` and overlapping AVX registers drastically outperform scalar string searches).