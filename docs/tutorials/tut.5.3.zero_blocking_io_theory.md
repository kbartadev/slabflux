# Tutorial 11: Zero-Blocking I/O (Durable Journaling)

## 1. The POSIX I/O Latency Trap

In high-throughput environments, financial transactions and critical state changes must be durably journaled to disk. However, traditional POSIX I/O (`write()`, `fsync()`, `pwrite()`) involves system calls that transition the thread into the kernel space, waiting on NVMe hardware interrupts. 

Executing a standard file write inside the Sovereign Core destroys your deterministic $O(1)$ latency bounds, introducing millisecond-level jitter and breaking pipeline execution.

## 2. The SlabFlux Journaling Topology

SlabFlux dictates that the Sovereign Core must **never** touch disk I/O directly. Durable journaling is achieved by chaining two mechanisms:

1.  **Lock-Free SPSC Conduit:** The Sovereign Core serializes the state to a pre-allocated cache-aligned buffer and pushes it to an outbound conduit via Monotonic Phase Matching (zero syscalls).
2.  **`io_uring` Storage Poller:** A dedicated, isolated OS thread polls the conduit and submits asynchronous I/O requests directly to the Linux kernel via `io_uring`. 

Because `io_uring` uses shared memory rings between User Space and Kernel Space, the Storage Poller can submit NVMe writes with exactly **zero system calls** (using `IORING_SETUP_SQPOLL`).

## 3. Hands-On: Sovereign Core to `io_uring`

Here is the architectural pattern for integrating a journal handler into your pipeline.

```cpp
#include <liburing.h>
#include <cstdint>
#include <cstring>

// 1. The Conduit Slot structure (perfectly cache-aligned)
struct alignas(64) journal_slot {
    uint64_t timestamp;
    uint64_t transaction_id;
    double executed_price;
    uint32_t padding[7];
    uint32_t phase_tag;
};

// 2. The Sovereign Core Handler (Deterministic, Zero-Blocking)
struct JournalHandler {
    journal_slot* outbound_ring;
    uint32_t ring_size;
    uint32_t current_idx = 0;
    uint32_t current_phase = 1;

    // Executed in the hot-path
    void on(const TradeExecutedEvent& e) {
        // Write payload to conduit slot
        outbound_ring[current_idx].timestamp = e.ts;
        outbound_ring[current_idx].transaction_id = e.id;
        outbound_ring[current_idx].executed_price = e.price;

        // Memory fence and publish phase
        __atomic_store_n(&outbound_ring[current_idx].phase_tag, current_phase, __ATOMIC_RELEASE);

        // Advance Phase
        current_idx = (current_idx + 1) & (ring_size - 1);
        if (current_idx == 0) [[unlikely]] {
            current_phase = (current_phase + 1) & 0xFF;
            if (current_phase == 0) current_phase = 1;
        }
    }
};

// 3. The Storage Poller (Isolated Thread on a separate core)
void io_uring_journal_poller(struct io_uring* ring, int fd, journal_slot* conduit) {
    uint32_t poll_idx = 0;
    uint32_t expected_phase = 1;
    uint64_t file_offset = 0;

    while (true) {
        uint32_t tag = __atomic_load_n(&conduit[poll_idx].phase_tag, __ATOMIC_ACQUIRE);
        
        if (tag == expected_phase) {
            // Get Submission Queue Entry from io_uring
            struct io_uring_sqe *sqe = io_uring_get_sqe(ring);
            
            // Prepare async write directly from the conduit memory (Zero-Copy)
            io_uring_prep_write(sqe, fd, &conduit[poll_idx], 60, file_offset);
            
            // Submit to kernel without blocking (batched later)
            io_uring_submit(ring);
            
            file_offset += 60; // Increment physical file offset
            
            // ... Phase advance logic ...
        }
    }
}
```

## 4. Best Practices & Anti-Patterns

*   **Best Practice: `O_DIRECT` and NVMe Sector Alignment.** Open your journal files with the `O_DIRECT` flag to bypass the OS page cache completely. Ensure your journal slot size perfectly matches the physical sector size of your NVMe drive (typically 512 or 4096 bytes) to maximize DMA throughput.
*   **Best Practice: SQPOLL Mode.** Initialize your `io_uring` with the `IORING_SETUP_SQPOLL` flag. This spawns a kernel thread that polls the Submission Queue (SQ), meaning `io_uring_submit()` does not actually trigger a system call, eliminating context switches entirely on the storage thread.
*   **Anti-Pattern: Dynamic Log Formatting.** Never use `std::string`, `snprintf`, or `std::ostream` in the Sovereign Core to format log lines. Serialization is slow. Dump binary structs directly to disk and parse them into human-readable text via an offline utility script.