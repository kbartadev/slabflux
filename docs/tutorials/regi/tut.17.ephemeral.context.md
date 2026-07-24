# Tutorial 17: The Ephemeral Context Engine & State Injection

In complex stateful systems—such as tracking a financial order's lifecycle or managing a player's session state—handlers need access to shared, mutable data. 

Traditionally, developers solve this by either bloating the event structure with temporary flags (`bool is_validated`, `int risk_score`), or by executing costly hash-map lookups inside the handler. Both approaches destroy CPU cache locality and increase memory overhead.

SLABFLUX solves this with the **Ephemeral Context Engine**. You can map an event to a specific "Context" object at compile time. The pipeline will automatically instantiate this context on the stack and inject it into your handlers alongside the event.

## 1. Defining Contexts and Mappings

A Context is a plain struct that holds the mutable state for a specific transaction. You explicitly map an event to a context using the `slabflux::meta::event_context_map`.

```cpp
#include "slabflux/core.hpp"
#include "slabflux/meta.hpp"

// 1. The pure, immutable event (Strictly data, no temporary state bloat)
struct client_login {
    uint64_t account_id;
    client_login(uint64_t id) : account_id(id) {}
};

// 2. The Context (Holds mutable state shared across the pipeline)
struct security_context {
    using slabflux_exclusive_event = client_login; // Mandatory binding
    bool is_banned = false;
    int security_clearance_level = 0;
};

// 3. The Compile-Time Mapping
namespace slabflux::meta {
    template <> 
    struct event_context_map<client_login> { using type = security_context; };
}
```

## 2. Context Injection in Handlers

When you write your handler, you simply request the context in the `on()` method signature. The `pipeline::dispatch()` mechanism detects this requirement at compile time and perfectly forwards the context by reference.

```cpp
#include <iostream>
#include "slabflux/core/pipeline.hpp"

struct ban_checker_stage {
    // The pipeline automatically injects the security_context!
    void on(const client_login& ev, security_context& ctx) {
        if (ev.account_id == 999) {
            ctx.is_banned = true;
        }
    }
};

struct authorization_stage {
    // Reads the state mutated by the previous stage
    void on(const client_login& ev, security_context& ctx) {
        if (ctx.is_banned) {
            std::cout << "[Auth] Connection rejected for ID: " << ev.account_id << "\n";
        } else {
            ctx.security_clearance_level = 5;
            std::cout << "[Auth] Clearance granted.\n";
        }
    }
};
```

## 3. Execution

The pipeline handles the lifecycle of the context automatically. It exists only for the duration of the `dispatch` call, meaning zero heap allocation and perfect stack utilization.

```cpp
int main() {
    ban_checker_stage ban_check;
    authorization_stage auth;
    
    // The matrix pipeline bundles the stages
    slabflux::core::pipeline<ban_checker_stage, authorization_stage> pipe(ban_check, auth);

    client_login login_ev(999);
    
    // The pipeline automatically constructs `security_context` on the stack,
    // passes it to ban_checker_stage, then passes the SAME context to authorization_stage.
    pipe.dispatch(&login_ev); 

    return 0;
}
```


