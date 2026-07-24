# Blueprint: http_avx.hpp

## Architectural Overview
Branchless state machine powered by AVX2/AVX-512 to extract HTTP/1.1 or REST payload boundaries in single-digit processor cycles, entirely replacing byte-by-byte scalar string loops.