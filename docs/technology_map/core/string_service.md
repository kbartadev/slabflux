# SlabFlux Core: String Service (`string_service.hpp`)

## 1. Architectural Overview
A centralized formatting and interpolation engine that completely replaces `<format>` and `<iostream>` on the execution hot path.

## 2. Compile-Time Formatting
Converts numbers, floats, and enum states into ASCII representations using unrolled SIMD division and binary-coded decimal (BCD) algorithms.

## 3. Zero-Allocation Appends
It writes directly to egress conduits in a single pass without allocating intermediate string buffers or moving pointers across functions.