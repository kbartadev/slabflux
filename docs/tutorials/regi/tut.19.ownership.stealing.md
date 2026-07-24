# Tutorial 19: Ownership Stealing & Pipeline Short-Circuiting

While raw pointers (`T*`) are the absolute standard for O(1) hot paths where the execution remains synchronous, there are critical architectural moments where a specific handler needs to **steal** the event from the framework.

For example, you might want to buffer the event in a state machine, move it into a lock-free background queue for deferred processing, or aggregate multiple packets before proceeding. If a handler *steals* the event, it is crucial that the rest of the pipeline does **NOT** continue executing, as the data is no longer safe to read.

SLABFLUX perfectly handles this via `scoped_ptr<T>` and **Automated Pipeline Short-Circuiting**.

## 1. The `scoped_ptr` Signature

By default, handlers request `const T*` or `T*`. However, if your handler explicitly requests a `slabflux::core::scoped_ptr<T>&`, the pipeline grants you direct access to the memory’s ownership wrapper.

```cpp
#include "slabflux/core/scoped_ptr.hpp"
#include <iostream>

struct trade_execution {
    uint64_t trade_id;
};

// A handler designed to steal events and move them to a background thread
struct deferred_processing_stage {
    
    // Notice the signature: taking the scoped_ptr by non-const reference
    void on(slabflux::core::scoped_ptr<trade_execution>& ev_ptr) {
        if (!ev_ptr) return;
        
        if (ev_ptr->trade_id % 2 == 0) {
            std::cout << "[Deferred Stage] Stealing trade ID: " << ev_ptr->trade_id << "\n";
            
            // STEAL THE OWNERSHIP!
            // .release() (or .detach()) strips the raw pointer out of the wrapper.
            trade_execution* raw_event = ev_ptr.release(); 
            
            // Now YOU own it. You can push it to an spsc_conduit, or save it for later.
            // background_queue.push(raw_event);
        } else {
            std::cout << "[Deferred Stage] Ignoring trade ID: " << ev_ptr->trade_id
                      << " (Leaving it in the pipeline)\n";
        }
    }
};
```

## 2. Automated Pipeline Short-Circuiting

The true magic of SLABFLUX lies in the `pipeline::dispatch` matrix. After invoking your handler, the compiler‑inlined pipeline checks the state of the `scoped_ptr`.

If it detects that `.release()` was called (meaning the pointer is now empty), **it immediately aborts the rest of the pipeline execution**. Downstream handlers will never see the stolen event.

```cpp
#include "slabflux/core/pipeline.hpp"

struct logging_stage {
    // This stage expects to log every event that reaches the end of the pipeline
    void on(const trade_execution* ev) {
        std::cout << "[Logging Stage] Recording trade: " << ev->trade_id << "\n";
    }
};

int main() {
    deferred_processing_stage stealer;
    logging_stage logger;
    
    // The pipeline executes 'stealer' first, then 'logger'
    slabflux::core::pipeline pipe(stealer, logger);

    // 1. Dispatching an ODD trade ID
    // The 'stealer' ignores it. The 'logger' WILL execute.
    slabflux::core::scoped_ptr<trade_execution> ptr1(/* assume allocated from pool */);
    ptr1->trade_id = 1337;
    
    std::cout << "--- Dispatching ODD Trade ---\n";
    pipe.dispatch(ptr1); 
    // Output:
    // [Deferred Stage] Ignoring trade ID: 1337
    // [Logging Stage] Recording trade: 1337

    // 2. Dispatching an EVEN trade ID
    // The 'stealer' takes ownership. The pipeline aborts. The 'logger' is NEVER called.
    slabflux::core::scoped_ptr<trade_execution> ptr2(/* assume allocated from pool */);
    ptr2->trade_id = 4040;
    
    std::cout << "\n--- Dispatching EVEN Trade ---\n";
    pipe.dispatch(ptr2);
    // Output:
    // [Deferred Stage] Stealing trade ID: 4040

    return 0;
}
```
