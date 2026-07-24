# Foundation: Timing Wheel (`slabflux/core/timing_wheel.hpp`)

## 1. Architectural Justification
Standard timer queues (like `std::priority_queue` or `epoll` timeouts) use min-heaps, which enforce an `O(log N)` sorting cost on every insertion and deletion. In an HFT execution loop processing millions of market data timeouts per second, logarithmic degradation is fatal. The `timing_wheel` provides mathematically guaranteed `O(1)` timer resolution.

## 2. Hardware Implementation Directives
- **Hashed Circular Arrays**: Time is modeled as a circular array of "slots," each representing a discrete clock tick (e.g., 1 microsecond). Inserting a timer is a direct array index calculation (`current_tick + delay & MASK`).
- **Zero-Sleep Polling**: The engine never invokes `nanosleep()` or `timerfd_create`. The wheel is advanced purely by polling the `__rdtsc()` physical clock during the non-blocking execution loop.
- **Cache-Oblivious Cascading**: For timers exceeding the primary wheel's circumference, hierarchical cascaded wheels are used. Bitwise shifts instantly demux long-duration timers down to the fast wheel as time progresses.

## 3. Bibliography & Proofs
1. **Varghese, G., & Lauck, T.** (1987). *Hashed and Hierarchical Timing Wheels: Data Structures for the Efficient Implementation of a Timer Facility*. Proceedings of the eleventh ACM symposium on Operating systems principles (SOSP).
2. **Varghese, G.** (2004). *Network Algorithmics*. Morgan Kaufmann. (Chapter 11: Timers).