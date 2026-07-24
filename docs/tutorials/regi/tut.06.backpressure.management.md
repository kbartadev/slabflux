# Tutorial 6: Backpressure and Deterministic Dropping

What happens when incoming network traffic (Producer) is faster than the processing thread (Consumer) can handle?

In traditional systems, memory usage skyrockets and the program crashes (OOM). In SLABFLUX, ring buffer sizes are fixed. When the buffer becomes full, the system applies deterministic backpressure. At that point, the developer must explicitly decide: either wait (as in the previous example) or drop the packet.

## 1. Safe Packet Dropping

If the channel fills up in a high‑priority HFT system or a real‑time game server, waiting for old data is pointless. The most recent data matters, and the old one must be dropped.

Since we are working with raw pointers, it is **mandatory** to immediately return the memory to the Pool when dropping, otherwise we cause a memory leak!

```cpp
void network_ingress_loop(slabflux::core::spsc_pool<work_task, 1024>& pool, 
                          slabflux::core::spsc_conduit<work_task*, 1024>& bus) {
    
    // We received a new network packet
    auto* current_task = pool.make(42);
    
    // try_push returns FALSE if the conduit is full.
    if (!bus.try_push(current_task)) {
        
        // The processing thread has fallen behind. System overload!
        std::cerr << "[Warning] System overloaded, dropping packet!\n";
        
        // CRITICAL: Since the conduit did not take ownership of the pointer,
        // we must clean it up ourselves!
        pool.release(current_task);
        return;
    }
}
```
