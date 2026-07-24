# Tutorial 5.2: Durable Hardware Journaling

## 1. Bypassing the Linux Page Cache
Standard file I/O (`std::ofstream`, `fwrite`) buffers data into the Linux Page Cache. This breaks deterministic latency because the OS flushes dirty pages to the physical disk asynchronously. If the page cache fills, or the OS decides to flush during a critical execution window, your thread will be blocked in an uninterruptible sleep state (`D` state in top/htop).

## 2. Direct NVMe Persistence (`durable_journal.hpp`)
SlabFlux provides `durable_journal` to guarantee constant-time persistence. It opens underlying block devices using the `O_DIRECT` flag. 

`O_DIRECT` explicitly bypasses the OS page cache. Memory is transferred via Direct Memory Access (DMA) straight from your user-space buffer to the NVMe controller.

## 3. Logical Sequence Number (LSN) States
To maintain a perfectly restorable state machine (Causal Mesh), the journal tracks Logical Sequence Numbers (LSNs). Every mutation of a Context Vault is appended to the journal with its corresponding LSN. In the event of a catastrophic hardware failure, the pipeline recovers by replaying the `durable_journal` sequentially up to the latest intact LSN.

### Hands-On: Hardware-Aligned Journaling

```cpp
#include "slabflux/io/durable_journal.hpp"
#include "slabflux/core/pipeline.hpp"

// Must perfectly align to the physical sector size of the NVMe drive (typically 512 or 4096 bytes)
struct alignas(512) AuditRecord {
    uint64_t logical_sequence_number;
    uint64_t executed_trade_id;
    double fill_price;
    // Padding automatically handled by the compiler due to alignas
};

struct ExecutionJournaler {
    slabflux::core::durable_journal* journal;

    void on(const AuditRecord& record) {
        // The durable_journal handles the O_DIRECT mapping.
        // Because AuditRecord is 512-byte aligned, the NVMe DMA engine 
        // can ingest this pointer without bounce-buffers.
        journal->append(&record, sizeof(AuditRecord));
    }
};

int main() {
    // Instantiate the journal pointing to a pre-allocated block device or raw partition
    slabflux::core::durable_journal main_journal("/dev/nvme0n1p2");
    
    ExecutionJournaler journaler{&main_journal};
    slabflux::core::pipeline<ExecutionJournaler> pipe(journaler);
    
    AuditRecord rec{
        .logical_sequence_number = 1001,
        .executed_trade_id = 99999,
        .fill_price = 4500.25
    };
    
    pipe.dispatch(rec);
}
```

## 4. Best Practices
*   **Strict Sector Alignment:** Failing to align your structs to 512 bytes (or 4096 bytes for Advanced Format drives) will cause `O_DIRECT` writes to fail instantly with an `EINVAL` error.