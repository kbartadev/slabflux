# Systolic Spatial Dataflow Substrate (`evaluating_cell.hpp`)

The autonomous GPU-side execution model of the **SlabFlux** architecture completely breaks with the Von Neumann principle and the classical SIMT (Single Instruction, Multiple Threads) paradigm. The system discards kernel launches, the grid/block/warp hierarchy, command queues, and `memcpy`-based data movement.

Instead, the GPU operates as a **Data-Driven physical graph**, where computation is a series of unidirectional state transitions.

---

## 1. Architectural Concept: Evaluating Cell

The GPU's hardware processing units (ALUs, Tensor cores) assume a static mathematical role (`MathFunctor`) during system initialization (Ignition Phase), becoming an **Evaluating Cell (`evaluating_cell`)**.

- **No Program Counter:** Cells do not read instructions from a global command queue. Instead, they act as infinite, uninterrupted state machines actively monitoring their physical memory addresses.
- **Trigger Logic:** Computation is executed exclusively when synchronized fresh data arrives in the cell's input registers. As soon as the result is computed, the cell writes it to its output register, which immediately becomes the input for the next cell in the graph.

---

## 2. Memory Model: Spatial Registers

Instead of dynamic VRAM allocations and Host-Device copies (`memcpy`), memory consists strictly of statically allocated **Spatial Registers** (`spatial_register`).

- **Hardware Geometry:** Every register is exactly 128 bytes in size (`alignas(128)`). This guarantees a perfect fit to the physical size of the GPU L2 cache sectors, completely eliminating False Sharing on the memory buses.
- **Zero-Copy:** Data transaction happens in-place via the local register assignments of the Evaluating Cells. There is no unnecessary data movement between layers.

---

## 3. Synchronization Rules: Monotonic Phase Matching

Warp-level synchronization (`__syncthreads()`, barriers) and classical atomic spinning (Compare-And-Swap) are eliminated. The memory model is Wait-Free and Lock-Free.

Synchronization is based on the principle of **Monotonic Phase Matching**:
1. Every 128-byte register contains an 8-bit (1 to 255) Phase Tag (`phase_tag`).
2. The Evaluating Cell continuously reads the input phases with strict `memory_order_acquire` semantics. C++20 `std::atomic_ref` ensures the atomic constraint on raw VRAM memory addresses.
3. **Trigger:** If both input phases (A and B) match the cell's internal expected phase (`expected_phase_`), the cell evaluates the payload.
4. **Closure:** After writing the output, the cell publishes the new phase with a `memory_order_release` barrier.
5. The O(1) deterministic wrap-around of phases is free of branch prediction logic (`& 0xFF`) and guarantees maximum ABA safety. The value `0` functions as a reserved VACUUM (empty) state.

---

## 4. Performance Invariants (Why is it faster than the classical SIMT GPU model?)

1. **Zero Kernel-Launch Latency:** With classical drivers, sending a CPU-built command and waking up the GPU hardware dispatcher consumes microseconds (2-10 µs). SSDS cells run statically and continuously, making hardware startup latency mathematically zero.
2. **No Warp Divergence:** In SIMT architectures, threads are locked in a 32/64 warp. If a thread branches, the warp must serialize instructions. SSDS Evaluating Cells are independent state machines without a shared Program Counter, so branches never degrade the silicon utilization of the ALUs.
3. **VRAM Bus Conservation:** During idle cycles, the instruction pipeline is deliberately blocked with a hardware instruction delay (`asm volatile("":::"memory")` – e.g., `s_sleep` or `nanosleep`), which radically reduces memory controller saturation during polling phases.