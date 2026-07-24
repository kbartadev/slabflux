# Tutorial 05: Hardware Isomorphism & SSDS Offload

## 1. Dismantling the Kernel-Launch Paradigm

Traditional GPU compute paradigms (CUDA, SYCL, Vulkan) introduce an abstraction layer between the CPU and GPU. Executing a kernel requires Ring 3 (User Space) to Ring 0 (OS Kernel) transitions, building command queues, and waking up the GPU hardware dispatcher. This routinely introduces 2-10 microseconds of jitter.

SlabFlux rejects this. We use the **Systolic Spatial Dataflow Substrate (SSDS)** model. 

- **No Kernel Launches:** The GPU is booted with a persistent, infinite-looping state machine (`evaluating_cell`). 
- **No Caches Evicted:** The CPU streams instructions physically across the PCIe bus bypassing its own L1/L2 cache.
- **State Transition Math:** Compute operations are triggered exclusively by mutating a specific physical memory address.

## 2. Host-Side: Non-Temporal PCIe Streaming

We map the GPU's memory into the CPU's memory space via the PCIe BAR (Base Address Register). The CPU uses AVX-512 non-temporal operations (`_mm512_stream_si512`) to write directly to the Write-Combining Buffers (WCB).

This achieves two things:
1.  It constructs an exact 64-byte Transaction Layer Packet (TLP) sent directly to the hardware.
2.  It prevents the deterministic AI/trading matrix from being evicted from the CPU's local L1/L2 caches.

```cpp
#include "slabflux/hw/isomorphic_matrix_bridge.hpp"

using namespace slabflux::gpu;

int main() {
    // Assume mapped_bar_ptr is allocated via HugePages pointing to the PCIe BAR
    emission_slot* mapped_bar_ptr = get_pcie_bar_mapping();
    
    isomorphic_matrix_bridge bridge(mapped_bar_ptr);

    // Instantly streams a 64-byte payload. Zero-blocking. Nanosecond latency.
    bridge.stream_evaluation_state(
        /* tensor_a_ptr */ 0x1000, 
        /* tensor_b_ptr */ 0x2000, 
        /* result_ptr   */ 0x3000, 
        /* dx */ 128, /* dy */ 128, /* op */ 1
    );
}
```

## 3. Device-Side: The Persistent Evaluating Cell

On the GPU side, we do not use thread blocks or dynamic warp scheduling. An Arithmetic Logic Unit (ALU) is statically assigned to an `evaluating_cell`.

It uses **Monotonic Phase Matching** for lock-free synchronization, completely bypassing `atomic_compare_exchange` and associated Read-For-Ownership (RFO) bus storms.

```cpp
// Represents the GPU-side execution logic defined in evaluating_cell.hpp
#pragma unroll 1 
while (true) {
    // Direct memory load with acquire barrier (compiles to PTX LD.CG or AMD global_load)
    uint32_t tag_a = __atomic_load_n(&input_a_->phase_tag, __ATOMIC_ACQUIRE);
    uint32_t tag_b = __atomic_load_n(&input_b_->phase_tag, __ATOMIC_ACQUIRE);

    if (tag_a == expected_phase_ && tag_b == expected_phase_) {
        
        // Execute in-place zero-copy mathematics
        MathFunctor::compute(input_a_->payload_matrix, 
                             input_b_->payload_matrix, 
                             output_->payload_matrix);

        // Release barrier publishing the new phase
        __atomic_store_n(&output_->phase_tag, expected_phase_, __ATOMIC_RELEASE);

        // O(1) determinisitic phase wrap-around
        expected_phase_ = (expected_phase_ + 1) & 0xFF;
        if (expected_phase_ == 0 || expected_phase_ == PHASE_KILL) [[unlikely]] expected_phase_ = 1;
        
    } else {
        // Hardware-specific thread yielding prevents VRAM controller starvation
#if defined(__AMDGCN__)
        asm volatile("s_sleep 1" ::: "memory");
#elif defined(__NVPTX__)
        asm volatile("nanosleep.u32 32;" ::: "memory");
#endif
    }
}
```

### Best Practices
*   Always ensure your `spatial_register` geometry is perfectly aligned with your GPU's L2 Cache Sector boundaries (`alignas(128)`) to eliminate False Sharing mathematically.
*   Isolate the Host-Side Ingress polling thread (`gpu_ingress_poller`) using `pthread_setaffinity_np` to secure microsecond bounds on the PCI-Express response plain.