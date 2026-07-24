# Blueprint: uring_ingress_xdp.hpp

## Architectural Overview
Provides ultimate zero-copy network reception. It binds directly to the network interface card (NIC) driver queues, intercepting physical frames before the Linux kernel allocates standard `sk_buff` structures.

## Core Logic & Mechanisms
- **UMEM Mapping (`xsk_umem`)**: Projects the application's memory pool arenas directly into the NIC's DMA engine. The hardware writes packets directly into local application memory.
- **Lock-Free RX Sweeping**: Reads the `xsk_ring_cons` completion ring natively. Casts incoming buffers instantly to typed structural matrices, triggering the pipeline sequentially without invoking the OS scheduler.
- **Fill Ring Replenishment**: Pushes consumed buffers immediately back into the `xsk_ring_prod` fill ring, guaranteeing continuous hardware operation bounds.