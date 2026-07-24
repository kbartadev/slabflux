# SlabFlux Core: Strategy Primitive (`slabflux/io/strategy.hpp`)

## 1. Architectural Justification
Defines the abstract interface for deterministic execution algorithms. It enforces strict mathematical isolation between state mutation (business logic) and physical I/O orchestration, ensuring the trading or business logic remains pure and testable.

## 2. Structural Directives
- **Pure Functions Only**: Strategy tick handlers receive standard C++ structs and return `void`. They are strictly prohibited from making network calls, allocating memory, reading files, or querying physical OS clocks.
- **Wait-Free Boundaries**: The strategy executes strictly within the context of the SPSC conduit poll loop. Its outbound states are pushed to egress multiplexers without yielding the thread.
- **Cache-Hot Proximity**: Strategy members are structurally padded using `alignas(64)` to ensure internal vectors and pricing matrices do not trigger L1 cache evictions against networking data.

## 3. Execution Integration
The strategy component forms the core of the `branchless_engine`. It acts as the ultimate consumer of the `network_conduit` matrix. When an inbound `sovereign_signal` completes its journey through parsers and demuxes, it is transformed into a pure typed state transition that triggers the strategy's deterministic tick.