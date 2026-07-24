# Tutorial 12: Bimodal Execution (Hot-Path vs. Cold-Path)

Not everything can—or should—be executed in O(1) time. While network routing and physics calculations belong in the "Hot Path", tasks like saving user profiles to a PostgreSQL database or calling an external REST API belong in the "Cold Path".

Bimodal Execution is the architectural pattern of splitting your pipeline: processing the critical payload instantly, and passing the heavy, stateful work to a background thread without slowing down the primary loop.

## 1. Splitting the Pipeline

We achieve Bimodal Execution by placing an `spsc_conduit` right in the middle of our logic. The Hot Path pushes the event into the conduit, and the Cold Path reads from it asynchronously.

```cpp
#include "slabflux/core/pipeline.hpp"
#include "slabflux/core/conduit.hpp"
#include "slabflux/core/pool.hpp"
#include <thread>
#include <iostream>

struct user_action {
    int user_id;
    int action_code;
    
    user_action(int uid, int code) : user_id(uid), action_code(code) {}
};

// ---------------------------------------------------------
// COLD PATH (Background Worker)
// ---------------------------------------------------------
void cold_path_worker(slabflux::core::spsc_pool<user_action, 1024>& pool, 
                      slabflux::core::spsc_conduit<user_action*, 1024>& async_bus) {
    while (true) {
        auto* ev = async_bus.pop();
        if (ev) {
            // Safe to execute slow, blocking operations here!
            std::cout << "[Cold Path] Saving User " << ev->user_id << " to database (Takes 5ms)...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); 
            
            // Terminal memory release happens in the cold path!
            pool.release(ev);
        }
    }
}

// ---------------------------------------------------------
// HOT PATH (Primary execution)
// ---------------------------------------------------------
struct fast_validator {
    slabflux::core::spsc_conduit<user_action*, 1024>& background_bus;

    fast_validator(slabflux::core::spsc_conduit<user_action*, 1024>& bus) : background_bus(bus) {}

    void on(user_action* ev) {
        if (!ev) return;
        
        std::cout << "[Hot Path] Validated action " << ev->action_code << " in 12 nanoseconds!\n";
        
        // Push to the cold path for persistent storage
        // If the background thread is too slow, try_push fails (Backpressure!)
        if (!background_bus.try_push(ev)) {
            std::cerr << "[Warning] Cold path is backed up! Dropping DB update.\n";
            // We must release memory if we failed to hand it off
            // pool.release(ev) would be needed here depending on architecture scope
        }
    }
};
```
