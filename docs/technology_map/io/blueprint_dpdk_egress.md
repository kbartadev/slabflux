# Blueprint: dpdk_egress.hpp

## Architectural Overview
High-velocity DPDK transmission engine. Maps outbound `wire_frame` pointers directly into pre-allocated `rte_mbuf` structures and transmits via batched `rte_eth_tx_burst` operations.