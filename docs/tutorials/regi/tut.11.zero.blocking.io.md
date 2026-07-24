# Tutorial 11: Zero-Blocking I/O (Durable Journaling)

In enterprise systems, every critical event must be saved to a database or disk (Journaling). However, standard `write()` syscalls block the thread while waiting for the NVMe/SSD to respond. In an HFT engine or Game Server, blocking the hot path for disk I/O is a fatal error.

SLABFLUX utilizes modern Linux `io_uring` through the `durable_sink` to stream data to disk asynchronously, bypassing the kernel context switch entirely.

## 1. The Durable Sink Pipeline

The `durable_sink` hooks directly into your cascading pipeline. It accepts your raw `POD` events, serializes them using the `trivial_serializer`, and queues them into the `io_uring` submission ring. Your hot path returns immediately (O(1)).

```cpp
#include "slabflux/storage/durable_sink.hpp"
#include "slabflux/core/pipeline.hpp"
#include "slabflux/core/pool.hpp"

// Must be trivially copyable!
struct alignas(64) audit_log {
    uint64_t timestamp;
    double metric_value;
    
    audit_log(uint64_t ts, double val) : timestamp(ts), metric_value(val) {}
};

int main() {
    slabflux::core::spsc_pool<audit_log, 1024> pool;
    
    // Initialize the durable sink, pointing to a fast NVMe drive
    slabflux::storage::durable_sink<audit_log> disk_journal("/mnt/nvme/audit.journal");

    // The sink acts as a handler in your pipeline
    slabflux::core::pipeline<slabflux::storage::durable_sink<audit_log>> pipe(disk_journal);

    // Create and dispatch
    auto* log_event = pool.make(1682390400, 42.5);
    
    // This call submits to io_uring and returns instantly. It NEVER blocks.
    pipe.dispatch(log_event);

    pool.release(log_event);
    return 0;
}
```
