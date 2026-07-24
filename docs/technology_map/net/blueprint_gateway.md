# Blueprint: gateway.hpp

## Architectural Overview
CRTP-based foundational interface for network routers. Enforces zero-allocation, wait-free execution, and strict `alignas(64)` memory bounds to prevent L1 instruction cache misses during polymorphic dispatch.