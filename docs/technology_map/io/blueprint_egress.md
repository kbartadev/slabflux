# Blueprint: egress.hpp

## Architectural Overview
Foundational CRTP base class for transmission modules. Enforces zero-allocation, wait-free execution, and strict memory alignment to standardize all specific egress implementations (DPDK, XDP, POSIX).