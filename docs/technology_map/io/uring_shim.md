# SlabFlux I/O: uring_shim (`slabflux/io/uring_shim.hpp`)

## 1. Architectural Justification
The `uring_shim` acts as an ultra-low-latency, zero-cost C++ abstraction over the raw `liburing` C API and the underlying kernel ABI. It insulates the higher-level SlabFlux I/O components from kernel-version discrepancies while strictly enforcing zero-allocation and inline-execution semantics.

## 2. Hardware Implementation Directives
- **Inline Expansion**: All SQE (Submission Queue Entry) preparations (e.g., `io_uring_prep_send`, `io_uring_prep_recv_multishot`) are wrapped in `__attribute__((always_inline))` methods. This guarantees that the C++ shim dissolves entirely during compilation, matching the exact instruction output of raw C macros.
- **Memory-Mapped Ring Exposure**: Provides direct, type-safe span access to the SQ and CQ ring memory boundaries. This prevents unintended pointer arithmetic errors while allowing the advanced components to execute lock-free ring traversal.
- **ABI Compatibility Guard**: Uses `static_assert` to verify alignment and struct packing of `io_uring_sqe` and `io_uring_cqe` structures against the configured CPU architecture, preventing silent memory corruption during `mmap` initializations.

## 3. Opaque Lifecycle Management
The shim securely binds the initialization (`io_uring_queue_init_params`) and teardown (`io_uring_queue_exit`) into RAII constructs. It ensures features like `IORING_SETUP_SQPOLL` or `IORING_SETUP_ATTACH_WQ` are perfectly synchronized with the surrounding multi-threaded execution topology.