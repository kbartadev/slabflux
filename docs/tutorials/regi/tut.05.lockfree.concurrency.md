# Tutorial 5: Lock-Free Communication Between Threads

In modern low-latency systems (whether it's a game server or a trading engine), the use of `std::mutex` and locks is strictly forbidden, because they cause operating-system-level context switches and blocking.

SLABFLUX handles inter-thread data transfer through a lock-free memory channel called `spsc_conduit` (Single-Producer Single-Consumer).

## 1. Structure of the SPSC Conduit

A `conduit` is a hardware‑optimized ring buffer utilizing C++20 `<bit>` operations for power-of-two masking and `std::hardware_constructive_interference_size` for physical isolation. This proprietary architecture distances the implementation from canonical LMAX patterns, moving **raw pointers (`T*`)** with absolute cache sovereignty.

```cpp
#include "slabflux/core/pool.hpp"
#include "slabflux/core/conduit.hpp"
#include <thread>
#include <iostream>

struct work_task {
    int payload;
    work_task(int p) : payload(p) {}
};

int main() {
    // 1. Memory is provided by the Pool (1024 elements)
    slabflux::core::spsc_pool<work_task, 1024> pool;

    // 2. The Conduit moves pointers between threads (1024 elements)
    slabflux::core::spsc_conduit<work_task*, 1024> bus;

    // Consumer thread
    std::thread worker([&]() {
        while (true) {
            // pop() is non-blocking! If the channel is empty, it returns nullptr.
            auto* task = bus.pop();

            if (task) {
                std::cout << "Processed: " << task->payload << "\n";
                // The consumer thread is responsible for freeing the memory!
                pool.release(task);
            }
        }
    });

    // Producer thread (Main)
    for (int i = 0; i < 5; ++i) {
        auto* task = pool.make(i);
        // Try to push into the channel (yield if full)
        while (!bus.try_push(task)) {
            std::this_thread::yield();
        }
    }

    worker.detach(); // For demonstration purposes
    return 0;
}
```
