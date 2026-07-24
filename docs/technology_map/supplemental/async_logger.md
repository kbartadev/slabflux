# SlabFlux Supplemental: Async Logger (`async_logger.hpp`)

## 1. Architectural Overview
Standard logging (e.g., `std::cout`, `printf`, or basic `spdlog`) executes blocking I/O system calls and acquires hidden global mutexes, making them fatal to deterministic latency. The `async_logger` provides a highly optimized, lock-free logging framework that offloads all I/O formatting and disk writing to a background thread.

## 2. Wait-Free Ring Injection
When a hot-path handler calls the log macro (e.g., `SLAB_LOG_INFO`):
1. The logger evaluates the arguments at compile-time and drops them into a trivially copyable `log_event` struct.
2. The struct is immediately pushed into a high-capacity `spsc_conduit` (or `mpmc_conduit` if multiple threads log).
3. The operation completes in under 10 nanoseconds. No string formatting, `snprintf`, or heap allocation occurs on the executing logic thread.

## 3. Background Formatting & I/O
A dedicated logging thread, pinned to a housekeeping CPU core, continuously drains the conduit:
- It pops the raw structs and formats them into human-readable strings.
- It invokes `writev` or `io_uring` to append the formatted text to the log files.
- If the log queue becomes full (due to excessive diagnostic spam), the hot-path `SLAB_LOG` macro deterministically drops the message rather than stalling the primary matrix, maintaining the supreme priority of the trading operations over diagnostics.