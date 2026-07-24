# SlabFlux Core: Context Vault (`context_vault.hpp`)

## 1. Architectural Overview
In the SlabFlux `pipeline`, handlers often require access to shared environmental state (e.g., configuration, hardware handles, or telemetry counters). The `context_vault` is a zero-overhead, type-safe container that provides this state to the dispatcher without relying on global variables or expensive thread-local storage lookups.

## 2. Compile-Time Geometry
The vault is a variadic template class (`context_vault<Ctx1, Ctx2, ...>`) that physically embeds all required context objects directly into its own memory layout.
- **Contiguous Allocation**: All context objects reside in a single, contiguous block of memory, maximizing L1/L2 data cache residency.
- **O(1) Access**: The `get<Ctx>()` method resolves the memory offset of the requested context type entirely at compile time. Accessing a context is a simple pointer-offset calculation, with zero runtime overhead.

## 3. Pipeline Integration
The `pipeline` dispatcher is designed to accept a `context_vault` as its first argument.

**Injection Lifecycle:**
1. The `unroll_ebases` function in the dispatcher inspects the `safe_context_assoc` trait for the current Event being processed.
2. It extracts the required context types (e.g., `typelist<MyContext, TelemetryContext>`).
3. The `execute_invoke` function then calls `vault.get<MyContext>()` and `vault.get<TelemetryContext>()` to retrieve the references.
4. These references are perfectly forwarded into the target `handler.on()` method.

## 4. Fallback Mechanisms
If a handler requests a context that is not present in the provided vault, the dispatcher gracefully falls back to the `get_global_context_instance()` mechanism. This thread-local singleton pattern provides a safety net for handlers that operate on globally-scoped (but still thread-isolated) state.