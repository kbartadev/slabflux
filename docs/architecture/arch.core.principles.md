# Core Architectural Principles

Any contribution or modification to the SLABFLUX core must respect these fundamental engineering principles.

## Structural Honesty

Code must perform exactly as it appears. No hidden memory offsets via unsafe casts. Use `union` for memory aliasing.

## Performance Integrity

- No virtual functions. Use CRTP and Concepts.
- No `std::shared_ptr`. Use `slabflux::event_ptr` for strict unique ownership.
- No global heap allocation. Heap allocation is banned in the hot path.

## Explicit Control

Implicit behavior is forbidden. Handlers must explicitly dictate the execution order of composed events (cascading execution).

## Physical Determinism

- Data structures must be cache-line aligned (`alignas(64)`) to prevent False Sharing.
- Systems must predictably survive saturation through deterministic drops.

## Zero Hidden Synchronization

Locks, mutexes, and semaphores are prohibited. All synchronization must be visible through lock-free atomic barriers.# Core Architectural Principles

Any contribution or modification to the SLABFLUX core must respect these fundamental engineering principles.

## Structural Honesty

Code must perform exactly as it appears. No hidden memory offsets via unsafe casts. Use `union` for memory aliasing.

## Performance Integrity

- No virtual functions. Use CRTP and Concepts.
- No `std::shared_ptr`. Use `slabflux::event_ptr` for strict unique ownership.
- No global heap allocation. Heap allocation is banned in the hot path.

## Explicit Control

Implicit behavior is forbidden. Handlers must explicitly dictate the execution order of composed events (cascading execution).

## Physical Determinism

- Data structures must be cache-line aligned (`alignas(64)`) to prevent False Sharing.
- Systems must predictably survive saturation through deterministic drops.

## Zero Hidden Synchronization

Locks, mutexes, and semaphores are prohibited. All synchronization must be visible through lock-free atomic barriers.# Core Architectural Principles

Any contribution or modification to the SLABFLUX core must respect these fundamental engineering principles.

## Structural Honesty

Code must perform exactly as it appears. No hidden memory offsets via unsafe casts. Use `union` for memory aliasing.

## Performance Integrity

- No virtual functions. Use CRTP and Concepts.
- No `std::shared_ptr`. Use `slabflux::event_ptr` for strict unique ownership.
- No global heap allocation. Heap allocation is banned in the hot path.

## Explicit Control

Implicit behavior is forbidden. Handlers must explicitly dictate the execution order of composed events (cascading execution).

## Physical Determinism

- Data structures must be cache-line aligned (`alignas(64)`) to prevent False Sharing.
- Systems must predictably survive saturation through deterministic drops.

## Zero Hidden Synchronization

Locks, mutexes, and semaphores are prohibited. All synchronization must be visible through lock-free atomic barriers.
