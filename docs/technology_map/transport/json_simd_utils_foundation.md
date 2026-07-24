# Foundation: JSON SIMD Utilities (`json_simd_utils`)

## 1. Architectural Justification
Finding string boundaries (`"`) or identifying unescaped control characters (`< 32`) in JSON payloads constitutes the vast majority of CPU cycles during parsing and serialization. Using standard library algorithms (`std::find_first_of`) heavily relies on scalar evaluation.

The `json_simd_utils` module isolates this string logic, utilizing wide AVX2/AVX-512 vector pipelines to evaluate up to 64 bytes simultaneously in a single processor cycle.

## 2. Hardware Implementation Directives
- **Overlapping Unaligned Tails:** Standard SIMD implementations drop to a slow scalar loop when processing the tail end of a string (< 32 bytes). SlabFlux uses overlapping loads (`pos = len - 32`), re-scanning bytes already processed but shifting them mathematically out of the final bitmask. This guarantees 100% vector saturation with zero scalar fallback.
- **Constant-Time Control Character Clamping:** To identify characters `< 32`, the utility uses `_mm256_max_epu8(data, 31)`. Any character beneath the threshold is clamped to 31. A subsequent equality check against 31 produces an exact mask of all control characters branchlessly.

## 3. Bibliography & Proofs
1. **Langdale, G., & Lemire, D.** (2019). *Parsing Gigabytes of JSON per Second*. VLDB Journal. (The mathematical foundation for branchlessly identifying structural characters within 256-bit SIMD lanes).
2. **Fog, A.** (2021). *Optimizing software in C++: An optimization guide for Windows, Linux and Mac platforms*. (Architectural guidelines on utilizing overlapping AVX loads to prevent loop tail performance degradation).