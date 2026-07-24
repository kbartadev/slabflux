# SlabFlux Core: String Service (`string_service.hpp`, `smart_string.hpp`)

## 1. Architectural Overview
Handling textual data (REST endpoints, FIX protocol fields, or telemetry logging) traditionally invokes dynamic heap allocations (`malloc`), which completely break determinism. 
The `string_service` subsystem provides a comprehensive, lock-free, zero-allocation string lifecycle manager built over fragmented chunk memory.

## 2. The Global String Pool
At ignition, the `global_string_pool` claims a massive, continuous block of memory and partitions it into thousands of `string_chunk` segments (strictly 64, 128, or 256 bytes long, `alignas(64)`).
- Threads access this pool via a wait-free SPSC channel.
- When a string needs to grow beyond its initial block, it instantly pops a new chunk and links it, establishing a non-contiguous but deterministic character array.

## 3. String Lifecycle Management
The `string_service` handles the garbage collection of `smart_string` objects:
- When an incoming HTTP request is parsed, the `demux_gateway` allocates chunked strings for the headers.
- Once the `pipeline` finishes evaluating the event, the `string_service` executes a `release_batch()`, instantly pushing the linked chain of chunks back into the free-list in a single O(1) atomic operation.
- Zero fragmentation occurs because all chunks are homogeneously sized.

## 4. View Projection
To interface with standard C++ APIs, the `string_service` seamlessly projects multi-chunk strings into segmented `std::string_view` iterations. This allows SIMD text-scanners (like the `http_avx` parser) to traverse the text rapidly without requiring contiguous memory blocks.