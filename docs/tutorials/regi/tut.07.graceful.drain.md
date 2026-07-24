# Tutorial 7: Graceful Drain and the Poison Pill

Operating‑system‑level interrupts (e.g., Ctrl+C or SIGINT) immediately terminate the program. If an O(1) pipeline is running at that moment, a sudden shutdown can lead to data corruption or partially processed states.

SLABFLUX handles shutdown using the “Poison Pill” architecture. We create a special event type or a flag that we send through the channel. When the consumer thread sees it, it performs a clean shutdown.

## 1. Implementing the Shutdown

```cpp
#include <atomic>
#include <csignal>

struct routine_task {
    bool is_poison_pill;
    int data;
    
    // By default, a normal event
    routine_task(int d) : is_poison_pill(false), data(d) {}
    // Special constructor for the poison pill
    routine_task(bool poison) : is_poison_pill(poison), data(0) {}
};

std::atomic<bool> global_shutdown{false};

void signal_handler(int) {
    global_shutdown.store(true, std::memory_order_release);
}

int main() {
    std::signal(SIGINT, signal_handler);
    
    slabflux::core::spsc_pool<routine_task, 1024> pool;
    slabflux::core::spsc_conduit<routine_task*, 1024> bus;

    std::thread worker([&]() {
        while (true) {
            auto* task = bus.pop();
            if (!task) { std::this_thread::yield(); continue; }
            
            // 1. Poison Pill check
            if (task->is_poison_pill) {
                std::cout << "Shutdown command received. Closing thread.\n";
                pool.release(task); // Cleanly release the poison pill as well!
                break;              // Exit the infinite loop
            }
            
            // 2. Normal processing
            pool.release(task);
        }
    });

    // Main loop
    while (!global_shutdown.load(std::memory_order_acquire)) {
        // Simulating normal operation...
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Initiate shutdown: inject the poison pill into the channel
    auto* pill = pool.make(true);
    while (!bus.try_push(pill)) {
        std::this_thread::yield(); // Wait until it fits into the conduit
    }

    worker.join(); // Wait for the thread to finish safely
    return 0;
}
```
