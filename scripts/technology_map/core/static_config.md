# SlabFlux Core: Static Configuration (`static_config.hpp`)

## 1. Architectural Overview
Parsing JSON or YAML configuration files at runtime introduces dynamic memory allocations (`std::string`, `std::map`), uncontrolled branching, and strings that must be matched continuously. 
The `static_config` module shifts the entire configuration parsing and validation phase to compile-time or strict ignition-time phases, rendering configuration lookups perfectly O(1) along the hot path.

## 2. `constexpr` Configuration Evaluation
By declaring configuration schemas as `constexpr` structs:
- The C++ compiler resolves numerical bounds, bitmasks, and network routing constants instantly.
- Values that are accessed by the `pipeline` or `vector_lane_engine` become baked-in constants in the generated machine code (e.g., directly encoded as immediate operands in `MOV` or `CMP` instructions) rather than requiring memory fetches.

## 3. Schema Enforcement
The config module guarantees type-safety across distributed components:
- It strictly enforces parameter bounds (`static_assert(MaxQueueDepth % 2 == 0)`) preventing downstream alignment faults in lock-free arrays.
- It evaluates macro variables injected from the CMake environment, altering `if constexpr` branch paths physically within the binary to strip out unused network bridges or telemetry logic completely.

## 4. Immutable Runtime Bridge
For parameters that must be loaded from external files at boot (but remain locked thereafter), the `static_config` maps to the `immutable_config` singleton. Once the `ignition_manifest` completes, this memory slab is sealed (often using `mprotect` with `PROT_READ`), physically preventing rogue pointers from corrupting the system's operational parameters mid-flight.