# SlabFlux RTE: Flow Controller (`flow_controller.hpp`)
# Flow Controller (`flow_controller.hpp`)

## 1. Architectural Overview
In an asynchronous event-driven system, if the network ingress loop processes packets faster than the compute logic can execute them, lock-free conduits will silently absorb the traffic until they reach physical saturation. This phenomenon (Buffer Bloat) destroys deterministic latency guarantees.
The `flow_controller` is a high-watermark congestion manager designed to throttle ingress at the hardware boundary before buffers saturate.
## Architekturális Koncepció
Típusbiztos (Type-Safe), O(1) determinisztikus Integral Control Loop (PI/PID-szerű szabályozás). Folyadékmechanikai alapokon nyugvó adatáramlás-szabályozó.

## 2. Hysteresis Logic (80/40 Rule)
The controller employs a classic hysteresis loop to prevent rapid micro-toggling of the network sockets:
- **High Watermark (80%)**: When the `mpmc_conduit` connecting the gateway to the engine hits 80% capacity, the controller issues a Backpressure Signal. The `io_uring` ingress pauses its submission queue polling.
- **Low Watermark (40%)**: The ingress remains physically paused until the compute engine drains the queue below 40% capacity. Only then is the ingress loop re-armed.

## 3. Upstream Backpressure Propagation
By halting the user-space polling, the controller forces the network queues to back up into the Linux Kernel (or AF_XDP ring), which in turn triggers hardware TCP Window Scaling collapse. This elegantly propagates the backpressure signal all the way back to the remote client over the network wire, preventing packet loss.

## 4. Starvation Bypassing
Control-plane messages (like failover commands) bypass the standard flow controller. They are routed via the dedicated `AdminBus`, ensuring that even under extreme data-plane congestion, the management layer retains absolute sovereignty over the application state.
## Működés
A `flow_controller` dekupálja a Conduit megfigyelését a tényleges szabályozási politikától (`integral_control_loop`). Az aktuális leterheltséget (load) az előre meghatározott alsó (40%) és felső (80%) Set-Point határokhoz viszonyítja, és egy egyszerű feltételes relációval állítja elő a Backpressure logikai flaget, elkerülve a bonyolult állapottér-kiértékeléseket.