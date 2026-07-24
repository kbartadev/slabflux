# SlabFlux Net: Virtual TCP Listener (`virtual_tcp_listener.hpp`)

## 1. Architectural Overview
In standard POSIX networking, `accept()` is a blocking system call. Even in non-blocking event loops (`epoll`), the application must context-switch into the kernel to fetch the newly established socket file descriptor.

The `virtual_tcp_listener` provides passive open semantics entirely in user-space, maintaining the strict zero-syscall invariants of the SlabFlux execution manifold.

## 2. The Wait-Free Backlog Queue
Instead of kernel-managed socket queues, the `virtual_tcp_listener` utilizes a `slabflux::core::spsc_ring_conduit<uint32_t>`.

- When a remote peer initiates a connection, the `tcp_gateway` and `tcp_flow_engine` process the `SYN` $\to$ `SYN-ACK` $\to$ `ACK` handshake silently in the background.
- Once the `tcp_flow_engine` validates the final `ACK` and transitions the connection to `PHASE_ESTABLISHED`, the `tcp_gateway` pushes the new `connection_id` into the listener's backlog queue.

This operation is completely Wait-Free. If the application is saturated and the backlog queue fills up, new connections are naturally ignored (causing the remote peer to retransmit `SYN`), mimicking standard TCP backlog drop behavior.

## 3. O(1) Non-Blocking Accept
The Business Logic calls `listener.accept()` during its standard polling cycle.

```cpp
virtual_tcp_socket accept() noexcept {
    uint32_t* conn_id = backlog_queue_.get_peek_slot(0);
    if (SL_EXPECT_TRUE(conn_id != nullptr)) {
        // Connection established, wrap and return
    }
    return virtual_tcp_socket(nullptr);
}
```
Because this relies on the lock-free conduit, `accept()` never blocks, never sleeps, and returns invalid sockets instantly when no connections are pending. This perfectly aligns with the Sovereign Core's requirement for flat, uninterrupted instruction cascades.

## 4. Pre-Allocated TCB Mapping
When `accept()` returns a valid `virtual_tcp_socket`, it does not allocate memory for a new connection state. It simply wraps a pointer to the statically pre-allocated `tcp_transmission_control_block` residing in the `tcp_gateway`'s spatial hash matrix (`tcbs_[id & 1023]`). 

This ensures that scaling from 1 to 10,000 concurrent connections requires exactly zero heap allocations.