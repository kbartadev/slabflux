# SlabFlux Core: Fixed String (`fixed_string.hpp`)

## 1. Architectural Overview
In high-performance environments, the standard `std::string` is fatal to determinism. Even with Small String Optimization (SSO), any payload exceeding 15/22 bytes immediately triggers a hidden OS heap allocation (`malloc`). 
The `fixed_string<N>` is a stack-based, trivially copyable text wrapper ensuring string manipulations never trap to the kernel memory manager.

## 2. Stack-Bounded Geometry
The `fixed_string<N>` defines its exact maximum length via template parameter at compile-time.
- The internal character array `char data_[N]` is allocated directly in-place (on the stack or directly inside a larger `alignas(64)` struct).
- Because it has a statically known footprint, inserting a `fixed_string` into a `spsc_conduit` transfers the text via a flat, hardware-accelerated memory copy, rather than passing a dangling pointer to external heap memory.

## 3. SIMD Parity and Truncation
When copying from chaotic networking bounds into a `fixed_string`:
- The string automatically and safely truncates input that exceeds `N - 1`, ensuring a deterministic `\0` null-terminator is always present, mathematically preventing Buffer Overflow vulnerabilities.
- It integrates with AVX2 string manipulation logic (`http_avx`), allowing the `vector_lane_engine` to perform bulk `memcpy` or equality comparisons across the internal array without traversing byte-by-byte.

## 4. Trivial Copyability
Because it lacks custom destructors or dynamic pointers, `std::is_trivially_copyable_v<fixed_string<N>>` evaluates to `true`. This allows the framework to persist the strings to the `durable_journal` using rapid, bit-perfect `io_uring` block transfers.