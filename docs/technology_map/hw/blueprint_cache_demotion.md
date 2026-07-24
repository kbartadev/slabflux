# Blueprint: cache_demotion.hpp

## Architectural Overview
Hardware cache management utility. Employs `_mm_cldemote` intrinsics on parsed network payload lines to forcefully migrate cold data to the L3 cache, protecting L1 residency for critical algorithm states.