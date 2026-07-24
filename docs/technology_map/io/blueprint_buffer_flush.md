# Blueprint: buffer_flush.hpp

## Architectural Overview
Hardware memory consistency guard. Manages CPU store fences (`_mm_sfence`) and precise cache-line evictions (`_mm_clwb`) to ensure asynchronous states hit global visibility rapidly.