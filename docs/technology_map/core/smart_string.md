# SlabFlux Core: Smart String (`smart_string.hpp`)

## 1. Architectural Overview
A hybrid string implementation bridging the gap between stack-allocated `fixed_string` payloads and bounded dynamic string processing, optimized specifically for zero-allocation parsing phases.

## 2. Small String Optimization (SSO) Mastery
Forces SSO bounds to the absolute limits of the cache line (e.g., 62 bytes).
- If a string perfectly fits, it sits completely inline inside the hot path memory structure.
- If a string exceeds these bounds, instead of trapping to the OS heap (`malloc`), it seamlessly spills into a pre-allocated slab provided by the active `context_vault`.