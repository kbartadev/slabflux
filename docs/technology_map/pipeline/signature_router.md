# SlabFlux Pipeline: Signature Router (`slabflux/pipeline/signature_router.hpp`)

## 1. Architectural Justification
Handlers may define various signatures for their callbacks (`const Event&`, `Event*`, with or without context). The `signature_router` dynamically maps the provided network event into the exact memory-safe signature expected by the handler, without relying on virtual function tables.

## 2. Compile-Time Implementation Directives
- **SFINAE Probing**: Uses `std::void_t` to probe handler classes at compile time. It identifies the most restrictive valid signature (e.g., checking for `has_on_cref_ctx_bool`).
- **Branchless Invocation**: Once the correct signature is identified at compile-time, it hard-codes the type-cast and invocation, eliminating runtime signature checking.

## 3. Pipeline Integration
Sits between the `dispatch_unroller` and the user's business logic. It securely casts the physical memory pointer of the event to the typed reference expected by the Strategy's `on()` method.