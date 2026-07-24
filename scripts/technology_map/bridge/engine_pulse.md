# SlabFlux Bridge: Engine Pulse (`engine_pulse.hpp`)

## 1. Architectural Overview
The `engine_pulse` subsystem operates as the high-throughput sequencing and transactional barrier mechanism within the SlabFlux RTE. It guarantees strict monotonic event ordering and gap-less sequence propagation across isolated lock-free rings without invoking dynamic memory allocations.

## 2. Global Sequence Authority
In a multi-producer environment, establishing a definitive "happens-before" timeline is critical for determinism.

### Pulse Shared State
The `pulse_shared_state` encapsulates the global Logical Sequence Number (`last_lsn`) into a strictly cache-aligned structure. This isolates the intensely updated atomic counter from surrounding data elements, neutralizing cross-thread cache invalidation.

### Monotonic Reservations
The `pulse_execution_context` allows threads to execute localized, stack-bound sequence generation (`reserve_next()`). This grants the logic cores the ability to deterministically advance the sequence clock using relaxed atomic operations before executing their business logic.

## 3. The SPSC Data Bridge
The `spsc_data_bridge` serves as the deterministic execution chassis that connects external lock-free ring buffers directly to the core logic domain.

**Execution Lifecycle:**
1. The `consume()` loop evaluates the ingress ring wait-free.
2. It seamlessly unpacks the ring elements into raw POD structures.
3. It injects the strictly monotonic `LSN`s directly into the payload.
4. It guarantees a hardware execution fence (`_mm_sfence` or equivalent `std::atomic_thread_fence(std::memory_order_release)`) before finalizing the loop.
5. It pushes the updated global sequential markers downstream, ensuring all passive observers see the finalized state exactly as it was evaluated by the primary logic core.