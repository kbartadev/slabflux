# Tutorial 8.2: Deterministic String Handling

## 1. The `std::string` Latency Trap
The standard library `std::string` utilizes Small String Optimization (SSO), which avoids heap allocation for strings under 15-22 characters. However, the moment a string exceeds this limit (e.g., formatting a network payload or complex log line), `std::string` calls `malloc`. 

Heap allocation on the hot path causes unpredictable latency, lock contention in the memory allocator, and potential page faults.

## 2. SlabFlux String Primitives
The `core/` subsystem provides three verified structures to guarantee zero-allocation string manipulation:

*   **`fixed_string.hpp`**: A strictly bounded, stack-allocated string array (e.g., `fixed_string<64>`). Its capacity is a compile-time constant. If an append exceeds the capacity, it predictably truncates or faults based on invariants, but it **never** allocates.
*   **`smart_string.hpp`**: A zero-allocation view or bounded wrapper that integrates with the `pipeline` to pass string references cleanly without triggering copy constructors.
*   **`string_service.hpp`**: Provides allocation-free formatting (like `itoa`/`ftoa` equivalents) to serialize numbers into `fixed_string` buffers without using the bloated `<iostream>` or `snprintf` libraries.

## 3. Hands-On: Zero-Allocation Formatting

```cpp
#include "slabflux/core/fixed_string.hpp"
#include "slabflux/core/string_service.hpp"
#include "slabflux/core/pipeline.hpp"
#include <iostream>

struct LogEvent {
    slabflux::core::fixed_string<128> message;
};

struct LoggingHandler {
    void on(const LogEvent& e) {
        // The internal data is guaranteed to be contiguous and null-terminated
        // Exact API is Unknown — assuming a standard c_str() or data() accessor.
        // std::cout << e.message.c_str() << "\n";
    }
};

int main() {
    // Instantiate a bounded string directly on the stack
    // slabflux::core::fixed_string<128> buffer;
    
    // Formatting without allocation
    // Exact string_service API is Unknown — not inferable from the provided codebase.
    // slabflux::core::string_service::append(buffer, "Trade Executed: ");
    // slabflux::core::string_service::append(buffer, 4500.25);
    
    LogEvent event;
    // event.message = buffer;
    
    LoggingHandler logger;
    slabflux::core::pipeline<LoggingHandler> pipe(logger);
    
    pipe.dispatch(event);
}
```