# Transport Layer and AVX2 Hardware Analysis

The `slabflux::transport` module guarantees that industry-standard protocols (e.g., HTTP) are parsed at line-rate, satisfying the framework's strict deterministic thresholds without invoking traditional software parsers.

## `slabflux::transport::http_request`
A highly specialized event object structurally padded using C++20 `std::hardware_constructive_interference_size` to ensure physical cache isolation. Incoming wire data is streamed directly into a 32-byte-aligned `raw_buffer`, a critical hardware prerequisite for executing unaligned SIMD (AVX2) register loads without incurring cycle penalties.

## `slabflux::transport::baremetal_parser`
An HFT-grade, strictly zero-allocation protocol parser that interfaces directly with silicon architecture.
* **AVX2 Scanning:** Deploys `_mm256_load_si256` to ingest and evaluate 32 bytes of payload per CPU cycle.
* **Masking & Bit Tricks:** Utilizes `_mm256_cmpeq_epi8` and `_mm256_movemask_epi8` to instantly isolate delimiter characters (spaces, CRLF) across the vector lane.
* **Hardware Search:** Leverages the `__builtin_ctz` (TZCNT hardware instruction) to pinpoint the exact memory index of the target character in absolute O(1) time.
* **Branchless Design:** Data trimming and payload evaluation are engineered to minimize logical branching, drastically reducing stress on the CPU's branch predictor.

> **Critical Warning Regarding `std::string_view` Lifecycles:**
> To eliminate memory copying overhead, the parser generates `std::string_view` objects that point directly into the raw physical bytes of the `raw_buffer` (Zero-Copy architecture). 
> When the wrapping `event_ptr` goes out of scope and returns to the Pool, its underlying memory region is immediately overwritten by the Pool's internal free-list pointers (`next`). 
> Downstream subscribers and business logic handlers are mandated to perform a deep copy (e.g., extracting to a `std::string`) **prior** to the event being reclaimed by the memory manager.
