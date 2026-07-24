# SlabFlux Sys: GPU Accelerator (`gpu_accelerator.hpp`)

## 1. Architectural Overview
The `gpu_accelerator` provides a unified boundary for tiering massive tensor operations off the deterministic CPU mesh and onto asynchronous, heterogeneous hardware (CUDA, HIP, Vulkan).

## 2. True Heterogeneous Offload
When compiled with `__CUDACC__` or `SLABFLUX_ENABLE_CUDA`, the architecture bypasses the CPU's AVX-512 `vector_lane_engine`.
- The state matrices (`memory_state` and `weight_matrix`) are expected to reside in Zero-Copy Pinned Memory (`cudaHostAlloc`) or Unified Memory.
- The CPU issues a non-blocking `cudaLaunchKernel` onto a specific `cudaStream_t`.
- Because it operates asynchronously on the stream, the CPU instantly returns to the hot-path event loop, allowing the network RX threads to continue processing unhindered while the GPU processes millions of parameters.

## 3. Transparent CPU Scalar Fallback
To ensure the engine remains fundamentally compatible across diverse hardware constraints (e.g., IoT edge devices or stripped validation servers), the component implements a transparent scalar fallback.
- If the target compiler lacks a GPU backend, the preprocessor prunes the kernel invocation.
- It inserts a deterministic CPU loop incorporating hardware-equivalent min/max scalar clamps (`fmaxf`, `fminf`).
This ensures mathematical consistency regardless of the underlying silicon deployment.