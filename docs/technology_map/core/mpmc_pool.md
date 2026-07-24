# SlabFlux Core: MPMC Pool (`mpmc_pool.hpp`)

## 1. Architectural Overview
While `pinned_allocator_spsc` handles single-threaded pipelines, distributed mesh architectures require concurrent object sharing. The `mpmc_pool` (Multi-Producer Multi-Consumer) manages thread-safe, lock-free memory leasing across all CPU cores on a given NUMA node.

## 2. Bounded Lock-Free Matrix
Instead of an unbounded heap, the `mpmc_pool` allocates a strict, fixed-size matrix of structures.
- It utilizes an atomic, ABA-immune free-list.
- Memory is retrieved and released using highly optimized `std::atomic::compare_exchange_weak` spin-loops.
- The backing structures are `alignas(64)` padded to prevent threads returning memory from invalidating the cache lines of threads requesting memory (False Sharing).

## 3. RAII Auto-Reclamation
To prevent catastrophic memory leaks in a system lacking garbage collection:
- The pool dispenses `managed_data<T>` smart pointers.
- When a `managed_data` object goes out of scope (e.g., after an event reaches the end of the `pipeline`), its destructor automatically returns the underlying physical pointer to the `mpmc_pool`.

## 4. Memory Exhaustion Agnosia
If the pool is completely exhausted, it does not throw `std::bad_alloc`. Instead, it returns `nullptr`, allowing the caller to gracefully route the failure to the `aphasic_horizon_` for deterministic load-shedding without unwinding the stack.