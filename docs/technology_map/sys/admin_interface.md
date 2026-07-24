# SlabFlux Sys: Admin Interface (`admin_interface.hpp`)

## 1. Architectural Overview
Interacting with a deterministic system in flight (e.g., shutting down the trading engine, updating risk limits, or triggering a hotpatch) requires injecting external commands without breaking chronological determinism. The `admin_interface` is a secure, out-of-band Remote Procedure Call (RPC) layer designed specifically for this purpose.

## 2. Out-of-Band Sovereign Gateway
The admin interface binds to a dedicated, heavily firewalled TCP socket.
- It operates entirely on the background housekeeping cores, utilizing `epoll` or blocking `recv` mechanisms without impacting the hot path.
- Administrators or cluster-orchestrators send commands (like `SET_RISK_LIMIT`) via secure protocols.

## 3. Deterministic Ingestion (`engine_pulse_bridge`)
Once an administrative command is authenticated:
1. The interface allocates an `admin_event` payload.
2. It drops the payload into the `engine_pulse_bridge`.
3. The `branchless_engine`, during its scheduled `AdminBus` polling slice, pulls the event.
4. The event is stamped with a live `LSN` (Logical Sequence Number) and executed synchronously as part of the causal mesh, ensuring that every node in the cluster applies the administrative limit update at the exact same logical microsecond.