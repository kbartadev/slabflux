# Blueprint: Hardware Topology & Alignment Overrides Architecture

## Architectural Overview
System alignment strictly obeys the physical geometries of the targeted processor cache, actively blocking false sharing and restricting execution logic to distinct socket boundaries.

## Header Mappings
- **`hot_path_alignment.hpp`**: Implements C++20 `std::hardware_constructive_interference_size` compiler constraints. Structs wrapped with these macros guarantee 64-byte or 128-byte cache-line isolation, rejecting unaligned dynamic padding.
- **`hardware_topology.hpp`**: Governs Thread and Memory affinity parameters. Utilizes `pin_thread_to_core` to force Linux `sched_setaffinity` constraints and locks kernel memory strictly against local NUMA node pathways.