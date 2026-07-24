# Blueprint: dpdk_ingress.hpp

## Architectural Overview
High-velocity DPDK reception engine. Uses vectorized `rte_eth_rx_burst` fetching, zero-copy payload slicing, and software prefetching to read incoming Ethernet frames without `sk_buff` allocations.