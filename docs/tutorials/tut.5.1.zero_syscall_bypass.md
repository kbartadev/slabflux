# Tutorial 5.1: Zero-Syscall Kernel Bypass

## 1. The Context Switch Penalty
Standard network programming via POSIX sockets (`recv()`, `epoll_wait()`) forces the CPU to process hardware interrupts and execute context switches from Ring 3 (User Space) to Ring 0 (Kernel Space). In a high-frequency trading (HFT) context, a 2-microsecond context switch is a massive topological disruption that destroys determinism.

SlabFlux explicitly bypasses standard sockets. The internal implementation within `io/io_uring_ingress.cpp` maps the kernel's completion and submission queues directly into user space memory.

## 2. `SQPOLL` and User-Space Polling
SlabFlux utilizes `io_uring` with the `IORING_SETUP_SQPOLL` flag. This spawns a kernel-side thread to poll the Submission Queue (SQ), allowing the Sovereign Core to submit read/write vectors via shared memory rings without a single Ring 3 $\to$ Ring 0 transition.

## 3. Network Conduits (`network_conduit.hpp`)
The `network_conduit` projects the conduit abstraction across physical TCP boundaries. It executes non-blocking `poll_rx` calls to move data from the NIC DMA buffers into the pipeline with zero copies.


## 4. Best Practices
*   **Avoid Syscalls at All Costs:** Never invoke `send()` or `recv()` on the Sovereign Core thread. Rely entirely on the memory-mapped `network_conduit`.
*   **Memory Pinning:** Ensure the buffers handed to the `network_conduit` are allocated via `pinned_allocator_spsc` to prevent the OS virtual memory manager from swapping active network buffers to disk.