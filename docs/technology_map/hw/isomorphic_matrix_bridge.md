# Isomorphic Hardware-Mapped State Matrix (GPU Offload)

The **SlabFlux Ontological Compute Subsystem** discards classical GPU runtimes (CUDA, Vulkan, SYCL, HIP). Communication between CPU and GPU does not occur through software abstraction layers (driver stack, command queue, kernel dispatch) nor via Ring 3 -> Ring 0 context switches. Instead, the system utilizes an **isomorphic memory matrix** that functions as directly shared memory space (BAR) over the PCIe bus.

This architecture applies the principle of *"Compute as a State Transition"*: task execution is simply the overwriting of dedicated physical memory addresses.
- **State Machine on the GPU:** There is no dynamic "kernel launch" or grid/block scheduling. During boot (Ignition Phase), the GPU loads a static evaluation phase machine (Persistent Thread) running in an infinite loop, actively monitoring BAR memory addresses.
- **Isomorphic Mapping:** The CPU's AVX-512 SIMD register lanes are mathematically and physically mapped 1:1 to the GPU's Streaming Multiprocessor (SM) blocks.

---

## 1. Memory Model and CPU↔GPU Data Flow

Data flow follows the strict **Zero-Copy** principle, avoiding cache invalidation storms (False Sharing) on shared addresses. The structure utilizes two physically separated, unidirectional memory planes consisting of Cache-line sized (64-byte) blocks. 
The actual data (tensors, weights) remain entirely within the Unified Pinned Memory space, in physical RAM locked by HugePages (2MB/1GB); the GPU DMA engine processes them transparently.

### A) Emission Plane (Host-to-Device)
- **Writes:** CPU (Deterministic core)
- **Reads:** GPU (Streaming Multiprocessors - SM)
- **Operation:** The CPU pushes mathematical task descriptors via an AVX-512 `_mm512_stream_si512` (Non-Temporal) instruction directly into the CPU's Write-Combining Buffer (WCB). This operation completely bypasses the CPU's L1/L2 cache hierarchy. The 64 bytes transition as a single PCIe transaction (TLP packet) to the GPU memory controller, sealed by a hardware `_mm_sfence()` (Store Fence) instruction to guarantee PCIe transaction ordering.

### B) Response Plane (Device-to-Host)
- **Writes:** GPU
- **Reads:** CPU Ingress Poller thread
- **Operation:** The GPU writes the pointers and integrity hashes of the completed tensor operations here. The isolated CPU thread reading this utilizes the x86-64 TSO (Total Store Order) memory model, monitoring state changes without C++ type-abstraction overhead (`__atomic_load_n`).

---

## 2. Lock-Free Synchronization and Phase Tagging

Classical Atomic Compare-And-Swap (CAS) memory spinning and expensive `LOCK` prefixed atomic counters (atomic add/sub) are entirely eliminated.

Synchronization is achieved using an **8-bit monotonically increasing Phase Tag**:
- The CPU embeds the phase tag into the last 4 bytes of the 64-byte `emission_slot`.
- The GPU continuously reads this tag (with strict `load-acquire` memory ordering). If the value matches the next expected phase, the SM executes the computation, then updates the phase tag of the `response_slot` to respond.
- ABA-safety is guaranteed by synchronized, O(1) branchless bitmask-secured wrap-around, completely eliminating phase collisions.

---

## 3. Preserving Determinism (Decoupled Gateway)

The strict logical timeline (Logical Sequence Number) of the deterministic AI core must not be compromised by anomalies (jitter) on the PCIe bus or GPU kernel blockages. Mathematical isolation is provided by the Decoupled Gateway:

1. The deterministic core exclusively performs **asynchronous, zero-blocking** memory writes (AVX-512 Stream) to the Emission Plane (`isomorphic_matrix_bridge`), then immediately (in O(1) step time, without branches) moves forward on the pipeline.
2. An isolated `gpu_ingress_poller` pinned to a topologically independent physical OS thread (SMT thread) performs continuous reading of the Response Plane.
3. As soon as the GPU yields a result, the Ingress Poller injects the response pointer into a standard Lock-Free SPSC (Single Producer Single Consumer) ring buffer (`conduit`).
4. The deterministic core processes this ring buffer as a fixed Event during its own Tick interval, thus hardware asynchrony is physically isolated.

---

## 4. Performance Invariants (Why is it faster than classical offloading?)

1. **Zero Driver / Kernel-Launch Overhead:** The CUDA/Vulkan driver stack consumes microseconds (2-10 µs) with Ring 3 -> Ring 0 context switches and command queue construction. With the Isomorphic Matrix, the time between the software instruction (Store) and the GPU waking up is purely the electrical propagation delay of the PCIe bus (approx. 300-500 ns).
2. **Scheduler Bypass:** The GPU phase machine runs persistently and directly monitors the PCIe BAR, so there is no grid/block/warp scheduling administration on the hardware dispatcher. Global memory contention is excluded.
3. **Zero Cache Eviction (L1/L2 Preservation):** Background state machines of classic drivers eject CPU cache lines. Non-Temporal WCB offload ensures data bypasses the cache, so the deterministic AI core's state geometry remains 100% in the L1/L2 cache.
4. **Zero Branching Operations:** Stepping offsets and phases is done with pure binary bitmasking (`&`) instead of the expensive modulo (`%`) operator, so the CPU's Branch Predictor is not burdened by synchronization.