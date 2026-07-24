# Blueprint: state_streamer.hpp

## Architectural Overview
Asynchronous memory archival component. Offloads deep-buffer NVMe disk writes from the primary algorithm loop using `io_uring` polling and DMA memory pinning, preserving event-sourcing determinism.