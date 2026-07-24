# SlabFlux Core: MPMC Conduit (`mpmc_conduit.hpp`)

## 1. Architectural Overview
A Multi-Producer Multi-Consumer lock-free ring buffer. Designed for many-to-many communication domains where traffic routing is non-deterministic (e.g., thread pools picking up background validation tasks).

## 2. Sequence Tickets
Uses centralized atomic fetch-and-add operations to distribute sequence tickets to producers and consumers. While heavier than SPSC conduits due to cache-line bouncing (RFO stalls), it provides exact chronological ordering across multiple writers.

## 3. Backoff Strategy
To prevent priority inversion and severe bus-locking, `mpmc_conduit` utilizes `_mm_pause` and progressive `std::this_thread::yield` fallbacks when a thread is stalled waiting for another producer to finish writing a payload.