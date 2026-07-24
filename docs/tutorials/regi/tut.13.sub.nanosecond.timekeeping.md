# Tutorial 13: Sub-Nanosecond Timekeeping

In standard applications, developers use `std::chrono::system_clock::now()` to measure time. In ultra-low latency environments, this is a fatal mistake. Even with Linux vDSO optimizations, standard clock calls can introduce microsecond-level latency spikes and disrupt the CPU pipeline.

For high-tickrate game physics or high-frequency trading (HFT) algorithms, you need precision down to the individual CPU cycle. SLABFLUX achieves this by directly querying the CPU's Time Stamp Counter (TSC).

## 1. Hardware-Level Timestamping

The `__rdtsc()` intrinsic reads a dedicated hardware register on the CPU that increments every single clock cycle. It requires zero operating system interaction, making it a true O(1), zero-latency operation.

```cpp
#include "slabflux/core.hpp"
#include <iostream>

#if defined(_MSC_VER)
    #include <intrin.h>
#else
    #include <x86intrin.h>
#endif

// A standard event containing a hardware timestamp
struct alignas(64) tick_event {
    uint64_t tsc_timestamp;
    double asset_price;
    
    tick_event(double price) : asset_price(price) {
        // Instantly capture the current CPU cycle count (Zero syscalls)
        tsc_timestamp = __rdtsc(); 
    }
};
```

## 2. Telemetry and Latency Measurement

You can use the TSC to measure the exact number of CPU cycles your logic takes to execute. This is how you prove your pipeline executes in nanoseconds.

```cpp
void measure_pipeline_latency(tick_event* ev) {
    uint64_t start_cycles = __rdtsc();
    
    // ... Execute complex pipeline logic here ...
    
    uint64_t end_cycles = __rdtsc();
    uint64_t cycles_elapsed = end_cycles - start_cycles;
    
    // On a 3.0 GHz CPU, 1 cycle is ~0.33 nanoseconds.
    // If cycles_elapsed is 30, your logic took exactly 10 nanoseconds.
    std::cout << "Pipeline executed in " << cycles_elapsed << " hardware cycles.\n";
}
```
