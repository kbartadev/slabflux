# Blueprint: endian.hpp

## Architectural Overview
Vectorized byte-swapping utility. Replaces scalar `ntohl()` with single-cycle register intrinsics (`__builtin_bswap64`) and AVX2 shuffle masks (`_mm256_shuffle_epi8`) to enforce instantaneous Network Byte Order translation.