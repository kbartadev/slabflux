# Foundation: Asymmetric Dispersion Queue (`slabflux/core/asymmetric_dispersion_queue.hpp`)

## 1. Architectural Justification
Multi-Producer Single-Consumer (MPSC) queues often unnecessarily penalize the consumer with heavy atomic operations designed for multi-consumer safety. The `asymmetric_dispersion_queue` capitalizes on the strict single-consumer invariant to completely eradicate `Compare-And-Swap` (CAS) instructions on the egress path.

## 2. Hardware Implementation Directives
- **Zero-CAS Consumer**: Since producers never overwrite a valid `T*` (they only transition `nullptr` to `T*`), the consumer executes a simple `load(acquire)` followed by a `store(nullptr, release)`. This avoids locking the memory bus via `LOCK` prefixes, dramatically increasing asymmetrical throughput.
- **Producer Spatial Dispersion**: Producers use hardware hashing to pick a row and execute a wait-free linear scan. This guarantees that multiple producers write to distinct cache lines simultaneously, preventing False Sharing.
- **Branchless Wrap-around**: Index wrapping is implemented via conditional subtraction (`if (col >= COLS) col -= COLS`) rather than integer modulo division (`DIV`), preserving ALU cycles.

## 3. Bibliography & Proofs
1. **Vyukov, D.** (2012). *Intrusive MPSC node-based queue*. 1024cores.net. (Foundations of asymmetric lock-free optimization and zero-CAS polling).
2. **Intel Corporation**. *Intel 64 and IA-32 Architectures Optimization Reference Manual*. (Performance disparity between locked atomic Read-Modify-Write instructions and standard release stores).