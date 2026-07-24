# Blueprint: uring_shim.hpp

## Architectural Overview
Ultra-low-latency, zero-cost C++ abstraction over the raw `liburing` API. Enforces inline-expansion and strict ABI alignment guarantees for SQ/CQ mapping, insulating the pipeline from kernel variations.