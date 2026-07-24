# Tutorial 3: Building Cascading Pipelines

SLABFLUX completely avoids OOP fragmentation. Instead of arrays of polymorphic events that scatter memory, the engine enforces **Composition** (Flattened Data) and **Cascading Dispatch**.

## 1. Composition over Inheritance

Compose your complex events from small, independent logical blocks. This ensures the data remains contiguous, trivially copyable, and fits perfectly into CPU cache lines.

```cpp
#include <string_view>
#include "slabflux/core.hpp"

// Independent POD data blocks
struct net_layer  { std::string_view ip; };
struct auth_layer { int session_id; };
struct app_layer  { std::string_view payload; };

// The composed aggregate event (Can represent a game state update or a network request)
struct client_request {
    net_layer net;
    auth_layer auth;
    app_layer app;

    client_request(std::string_view ip_addr, int sid, std::string_view p) {
        this->net.ip = ip_addr;
        this->auth.session_id = sid;
        this->app.payload = p;
    }
};
```

## 2. The Cascading Processor
Handlers are organized in a strict, waterfall-style sequence. The compiler evaluates the pipeline::dispatch routing at compile-time and inlines the entire pipeline into a single, flat execution path.

```cpp
#include <iostream>
#include "slabflux/core/pipeline.hpp"
#include "slabflux/core/runtime_domain.hpp"

struct firewall_stage {
    void on(net_layer& net) { std::cout << "[Firewall] Checking IP: " << net.ip << "\n"; }
};

struct auth_stage {
    void on(auth_layer& auth) { std::cout << "[Auth] Validating Session: " << auth.session_id << "\n"; }
};

// The Top-Level Processor driving the sequence
struct request_processor {
    firewall_stage fw;
    auth_stage au;

    // Dispatched via raw pointer directly from the pipeline
    void on(client_request& ev) {
    
        // Strict, compile-time inlined execution sequence
        fw.on(ev.net);
        au.on(ev.auth);
        
        std::cout << "[App] Processing payload: " << ev->app.payload << "\n";
    }
};

int main() {
    slabflux::core::runtime_domain<client_request> domain;
    request_processor processor;
    
    // The pipeline embeds the top-level processor
    slabflux::core::pipeline<request_processor> pipe(processor);

    // Make an event and dispatch it seamlessly (O(1) cost)
    auto req = domain.make<client_request>("192.168.1.100", 1337, "MOVE_FORWARD");
    pipe.dispatch(req);

    // Terminal memory release required at the end of the lifecycle
    domain.release(req);

    return 0;
}
```
