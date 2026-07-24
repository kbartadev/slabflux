# Tutorial 4.1: Systolic Dataflow (SSDS) Offload

## 1. Bypassing the Driver Stack
Traditional heterogenous compute models (CUDA, HIP, OpenCL) rely on driver-mediated "kernel launches." Submitting a task requires transitioning from Ring 3 (User Space) to Ring 0 (OS Kernel), constructing a command queue, and waking up the GPU's hardware dispatcher. This incurs 2 to 10 microseconds of latency.

SlabFlux dictates a strict **Driver-Bypass** model utilizing the Systolic Spatial Dataflow Substrate (SSDS).

## 2. Host-Side: PCIe Write-Combining (`isomorphic_matrix_bridge.hpp`)
The Sovereign Core streams 64-byte `emission_slot` instructions using **AVX-512 Non-Temporal streams** (`_mm512_stream_si512`). These bypass the CPU's L1/L2 cache hierarchy entirely, writing directly to the **Write-Combining Buffers (WCB)** for nanosecond-tier PCIe BAR dispatch.

## 3. Device-Side: The Persistent State Machine (`evaluating_cell.hpp`)
The GPU executes a static, data-driven spatial graph. Computation is triggered solely by the arrival of data via **Monotonic Phase Matching**. The GPU cells continuously poll VRAM with `memory_order_acquire`; when the `phase_tag` increments, the ALU executes the `MathFunctor` in-place.

### Hands-On: Zero-Latency Hardware Dispatch

```cpp
#include "slabflux/hw/isomorphic_matrix_bridge.hpp"

// Event containing the physical addresses of the tensors to multiply
struct TensorComputationRequest {
    uint64_t tensor_a_ptr;
    uint64_t tensor_b_ptr;
    uint64_t result_ptr;
};

// The Hardware Dispatch Handler
struct GPUOffloadEngine {
    slabflux::gpu::isomorphic_matrix_bridge* bridge;

    void on(const TensorComputationRequest& req) {
        // Streams 64-byte TLP packet via WCB. 
        bridge->stream_evaluation_state(
            req.tensor_a_ptr, 
            req.tensor_b_ptr, 
            req.result_ptr, 
            1     // Operation Code (e.g., 1 = Multiply)
        );
        
        std::cout << "[HW] Mathematical payload streamed to SSDS.\n";
    }
};

int main() {
    // Acquire the physical pointer to the PCIe BAR (Ignition Phase logic)
    slabflux::gpu::emission_slot* bar_ptr = nullptr; // e.g., via mmap /dev/mem
    slabflux::gpu::isomorphic_matrix_bridge hw_bridge{bar_ptr};
    
    GPUOffloadEngine engine{&hw_bridge};
    slabflux::core::pipeline<GPUOffloadEngine> pipe(engine);
}
```

**Constraint:** The payload mapped over the PCIe bus *must* be exactly 64 bytes (`alignas(64)`) to map perfectly into a single Transaction Layer Packet (TLP).