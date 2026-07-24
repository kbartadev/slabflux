# Blueprint: tcp_stream_defragmenter.hpp

## Architectural Overview
Zero-allocation sliding-window engine that scans overlapping TCP segments using AVX2 hardware masking to reconstruct logical application frames directly against DMA-mapped memory.