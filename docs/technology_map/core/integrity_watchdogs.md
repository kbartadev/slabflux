# SlabFlux Core: Integrity Watchdogs (`integrity_watchdogs.hpp`)

## 1. Architectural Overview
Hardware-backed, continuous timeline monitors designed to verify that no execution thread is ever starved, locked in an infinite loop, or diverging from the causal sequence clock.

## 2. RDTSC Pinging
Hot path logic loops "pet" the watchdog by writing their current LSN and `__rdtsc()` timestamp to a localized cache line, executing with zero hardware fencing instructions.

## 3. Sub-Microsecond Mitigation
A dedicated housekeeping core constantly checks the delta of these markers against acceptable cycle budgets. If a stall is detected (e.g., an unhandled memory fault), it instantly engages the `failover_orchestrator` to shoot down the local node and promote a standby replica.