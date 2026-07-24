# Blueprint: header_parser.hpp

## Architectural Overview
O(1) deterministic dispatch engine. Extracts fundamental protocol Type IDs directly from DMA-pinned memory using jump-tables and `alignas(64)` pointer overlays without dynamic allocation.