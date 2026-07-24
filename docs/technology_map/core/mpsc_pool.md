# SlabFlux Core: MPSC Pool (`mpsc_pool.hpp`)

## 1. Architectural Overview
The Multi-Producer Single-Consumer (MPSC) pool is tailored for scenarios where many peripheral systems (e.g., networking threads or IO polling loops) feed memory structures to a single deterministic compute core.

## 2. Asymmetric Synchronization
- **Single Consumer**: The single logic thread pops memory without requiring heavy atomics.
- **Multi-Producer Submit**: Producers utilize an intrusive, lock-free queue approach to submit recycled memory back to the pool, minimizing cache-line bouncing and eliminating `std::mutex` blocks on the ingestion side.