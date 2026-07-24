# Tutorial 06: Zero-Copy Networking & Causal Meshes

## 1. The Kernel Bypass Imperative

Standard networking in Linux relies on the kernel's network stack (socket buffers, `sk_buff`, TCP/IP processing). This introduces context switches (Ring 3 to Ring 0), dynamic memory allocations, and interrupts, leading to unpredictable tail latencies spanning dozens of microseconds.

SlabFlux dictates a strict kernel-bypass model utilizing **DPDK** (Data Plane Development Kit) or **AF_XDP**. 

The NIC's Direct Memory Access (DMA) engine writes packets directly into pre-allocated, physically pinned HugePages. The Sovereign Core never invokes `read()` or `recv()`; it merely polls physical memory states.

## 2. Bridging Ingress to the Execution Manifold

A core principle of SlabFlux is **Zero-Copy**. When a packet arrives, its payload is never copied into a new event struct. Instead, the physical memory pointer of the raw packet is injected into a Lock-Free Conduit, and the pipeline operates *directly* on the DMA buffer.

The architecture requires two topologies:
1.  **The Ingress Poller (Isolated Thread):** Dedicated exclusively to polling the NIC's descriptor rings.
2.  **The Sovereign Core (Deterministic Thread):** Reads pointers from the SPSC Conduit and dispatches the execution manifold.

## 3. Hands-On: DPDK Ingress to SPSC Conduit

Here is a structural abstraction of injecting a DPDK payload into a SlabFlux pipeline.

```cpp
#include <rte_ethdev.h>
#include <cstdint>

// Assuming the SPSC Conduit from Tutorial 04
struct alignas(64) conduit_slot {
    void* payload_ptr;
    uint32_t payload_len;
    uint32_t padding[10];
    uint32_t phase_tag;
};

// 1. The Ingress Thread (Pinned to NUMA Node 0, Core 1)
void rx_ingress_poller(uint16_t port_id, conduit_slot* conduit, uint32_t& current_phase) {
    struct rte_mbuf* bufs[32];
    uint32_t conduit_idx = 0;

    while (true) {
        // Zero-copy burst fetch from the NIC
        const uint16_t nb_rx = rte_eth_rx_burst(port_id, 0, bufs, 32);
        
        for (uint16_t i = 0; i < nb_rx; i++) {
            // Extract physical pointer to the packet payload
            void* raw_payload = rte_pktmbuf_mtod(bufs[i], void*);
            uint32_t len = rte_pktmbuf_data_len(bufs[i]);
            
            // Inject directly into the SlabFlux SPSC Conduit
            conduit[conduit_idx].payload_ptr = raw_payload;
            conduit[conduit_idx].payload_len = len;
            
            // Hardware Store Fence to guarantee ordering before phase publication
            asm volatile("sfence" ::: "memory");
            
            // Publish Phase Tag (Release)
            __atomic_store_n(&conduit[conduit_idx].phase_tag, current_phase, __ATOMIC_RELEASE);
            
            // Advance Conduit
            conduit_idx = (conduit_idx + 1) & (1024 - 1); // Assuming 1024 ring size
            if (conduit_idx == 0) [[unlikely]] {
                current_phase = (current_phase + 1) & 0xFF;
                if (current_phase == 0) current_phase = 1;
            }
        }
    }
}
```

## 4. Best Practices & Anti-Patterns

*   **Best Practice: Pre-Allocated Slabs.** The `rte_mempool` holding your packets must be backed by 1GB HugePages (`MAP_HUGETLB`). This guarantees the CPU's Translation Lookaside Buffer (TLB) will not miss when the Sovereign Core dereferences the payload pointer.
*   **Best Practice: NUMA Locality.** The NIC, the memory pool, the Ingress thread, and the Sovereign Core must reside on the same NUMA node. Crossing the UPI/Infinity Fabric destroys latency bounds.
*   **Anti-Pattern: Deserialization Copies.** Do not parse a packet by copying bytes into a local C++ `struct`. Use `reinterpret_cast` or `std::bit_cast` to overlay the C++ structure onto the raw pointer provided by the conduit. Validate bounds, then read directly.
*   **Anti-Pattern: Blocking Waits.** The Ingress thread must never sleep, yield to the OS, or use `epoll`. It must continuously poll the NIC descriptor ring to prevent hardware queue overflow and packet drops.