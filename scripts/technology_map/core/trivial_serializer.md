# SlabFlux Core: Trivial Serializer (`trivial_serializer.hpp`)

## 1. Architectural Overview
In deterministic, high-frequency environments, serialization frameworks like Protobuf or JSON introduce devastating dynamic memory allocations and CPU branching. The `trivial_serializer` is a mathematically guaranteed zero-overhead encoding mechanism used strictly for internal cluster communication.

## 2. Compile-Time Memory Parity
The serializer relies entirely on C++ type traits, specifically `std::is_trivially_copyable_v<T>`.
- By enforcing that all cluster-bound domain events are Plain Old Data (POD) structures without pointers, virtual functions, or dynamic strings, the serializer mathematically guarantees that the memory footprint of the object is identical to its network representation.
- **Zero-Cost Serialization**: Generating a network packet consists of a single `__builtin_memcpy` (lowered to AVX-512) directly from the C++ struct into the TCP/UDP egress buffer.

## 3. Endianness and Alignment
Because the SlabFlux mesh operates exclusively on homogenous, closely-coupled bare-metal servers (e.g., all x86-64 Linux machines), the `trivial_serializer` intentionally abandons `htonl`/`ntohl` byte-swapping.
- Skipping Endian conversions saves multiple CPU cycles per integer field.
- To preserve AVX-512 read performance upon deserialization, the serializer ensures that the payload is packed with `alignas(64)` padding, allowing the receiving node to `reinterpret_cast` the raw network buffer back into the C++ struct instantly without triggering hardware unaligned-load penalties.

## 4. Struct Versioning
To support zero-downtime upgrades where Node A and Node B might run slightly different binaries:
- The serializer implicitly prepends a 16-bit `VersionID` extracted from a `constexpr` trait on the target struct.
- If a struct layout is modified in the source code without incrementing the `VersionID`, the `meta_compiler` physically blocks the build.