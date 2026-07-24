# Tutorial 8.1: Jitter-Free Timers & Sequencing

## 1. The Cost of Hardware Timers
In standard applications, setting a timeout involves OS-level constructs like `epoll_wait`, `timerfd`, or `std::this_thread::sleep_for`. These mechanisms yield the CPU core to the operating system. When the timer expires, the OS must schedule a context switch to wake the thread, introducing 10 to 50 microseconds of latency jitter.

The SlabFlux Sovereign Core never yields. It polls continuously. Therefore, we need an $O(1)$ in-memory structure to manage time.

## 2. The `timing_wheel.hpp`
The `timing_wheel` is a purely mathematical construct that tracks expirations in fixed logical slots. 

*Note: The exact configuration APIs of the `timing_wheel` (e.g., slot sizes, hierarchical cascades) are Unknown — not inferable from the provided codebase.*

Architecturally, the `sovereign_core` updates the wheel using a hardware clock reading (like `__rdtsc()`) at the start of every polling cycle. If the wheel's current logical slot advances, it instantly dispatches the expired events down the `pipeline` without a single system call.

## 3. The `sequence_generator.hpp`
Global atomic counters for generating unique identifiers (like Order IDs or span IDs) cause cache-line bouncing across sockets. The `sequence_generator` provides core-local, deterministic sequence stamping, often tied to the physical topology (e.g., embedding the NUMA node ID or pipeline lane ID directly into the higher-order bits of the sequence).

## 4. Hands-On: Driving the Timing Wheel

```cpp
#include "slabflux/core/timing_wheel.hpp"
#include "slabflux/core/pipeline.hpp"
#include <x86intrin.h>

struct TimeoutEvent { uint64_t sequence_id; };

struct TimerHandler {
    void on(const TimeoutEvent& e) {
        // Execute deterministic logic upon timeout
    }
};

int main() {
    TimerHandler handler;
    slabflux::core::pipeline<TimerHandler> pipe(handler);
    
    // Instantiate the wheel (API details inferred conceptually)
    // slabflux::core::timing_wheel wheel;
    
    // In the Sovereign Core polling loop:
    while (true) {
        // 1. Read hardware clock (extremely low latency)
        uint64_t current_cycles = __rdtsc();
        
        // 2. Advance the wheel and dispatch expired events to the pipeline
        // Exact timing_wheel API is Unknown — not inferable from the provided codebase.
        // wheel.advance(current_cycles, pipe);
        
        // 3. Poll other conduits...
    }
}
```