# Blueprint: retransmission_buffer.hpp

## Architectural Overview
O(1) sliding window cache for outbound frames. Provides garbage-collection-free eviction and zero-copy resubmission leveraging hardware cache demotion (`_mm_cldemote`) during NACK processing.