# Blueprint: hardware_aux.hpp

## Architectural Overview
Privileged C++ API for silicon-level system isolation. Manages thread pinning, NUMA locality mappings, and MSR/C-state locks to guarantee uninterrupted deterministic execution and O(1) processing bounds.