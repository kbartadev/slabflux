# SlabFlux Transport: SIMD-Accelerated Lexer (`http_avx.hpp`)

## 1. Architectural Overview
The `http_avx_parser` is the ultra-high-throughput, SIMD-accelerated counterpart to the baseline `http_parser`. It maintains **100% mathematical and behavioral equivalence** to the scalar parser while replacing character-by-character DFA evaluation with vectorized hardware sieving.

## 2. Branchless Hardware Sieving
Instead of iterating through the buffer with a scalar loop, the AVX parser leverages 256-bit (AVX2) or 512-bit (AVX-512) wide vector registers to scan up to 64 bytes of the network buffer in a single clock cycle.

- **`_mm256_cmpeq_epi8` / `_mm256_testz_si256`**: The parser identifies delimiters (`\n`, `:`, ` `) natively in the vector domain. The `vptest` instruction sets the Zero Flag (ZF) directly, completely eliminating the latency of moving masks to General Purpose Registers unless a hit is guaranteed.
- **Trailing Zeros (`tzcnt`)**: Once a delimiter is found within the vector, `__builtin_ctz` yields the exact memory offset in $O(1)$ time, entirely bypassing branch prediction penalties.

## 3. Zero-Penalty Loop Tails
Standard SIMD implementations fall back to a slow scalar `while` loop when the remaining string length is less than the register width (e.g., the last 15 bytes of a header). 

SlabFlux eradicates this using **Overlapping Tails** and **Masked Loads**:
- **AVX-512**: Uses `_mm512_maskz_loadu_epi8` to natively suppress hardware page faults on bytes outside the buffer bounds.
- **AVX2**: Performs an overlapping unaligned load (`len - 32`), re-scanning a few bytes but guaranteeing a strictly branchless, vector-only tail resolution without ever crossing forbidden memory boundaries.

## 4. Integer Underflow Tokens
To validate RFC 9110 token constraints (rejecting spaces, tabs, and control characters in header keys), the parser uses a single-instruction arithmetic optimization:
```cpp
if (static_cast<unsigned char>(c - 33) > 93) return false;
```
By relying on unsigned integer underflow, characters below ASCII 33 (`!`) wrap around to massive positive integers, instantly failing the `> 93` check alongside extended ASCII characters (`> 126`). This validates token integrity in one CPU cycle without dual boundary checks.

## 5. Branchless Lowercasing (`ieq` / `icontains`)
Case-insensitive string comparisons use arithmetic masking rather than `if/else` logic:
```cpp
ca |= (static_cast<unsigned int>(ca - 'A') <= 25) ? 0x20 : 0;
```
This applies the `0x20` lowercase bitmask strictly within the alphabetical range without interrupting the execution pipeline.